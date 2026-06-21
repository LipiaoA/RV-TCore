# RV-TCore Kanata Log 实现方案

本文档描述如何在 RV-TCore 中生成 [Konata](https://github.com/shioyadan/Konata) 可读的 **Onikiri2-Kanata** 格式（格式一）流水线 trace，用于指令级流水线可视化。

---

## 1. 目标与约束

### 1.1 目标

在仿真运行期间生成 Konata 可读的 `.log` 文件，可视化指令在流水线各阶段的驻留、stall、依赖和 flush。

### 1.2 Kanata 格式约束

- **文件头**：`Kanata\t0004`（Tab 分隔）
- **正文**：Tab 分隔命令（`I` / `L` / `S` / `E` / `R` / `W` / `C`）
- **时间单向推进**：同一周期内先写事件，再用 `C 1` 推进
- **指令生命周期**：每条指令先 `I`，再 `S`/`L`/`W` 等，结束用 `R`（退休 `TYPE=0`，flush `TYPE=1`）

官方格式说明：[docs/kanata-log-format.md](https://github.com/shioyadan/Konata/blob/master/docs/kanata-log-format.md)

### 1.3 与现有代码的关系

| 现有组件 | 关系 |
|----------|------|
| `InstrTrace`（`include/log/instr_trace.h`） | 仅记录 commit 退休；Kanata 需覆盖全流水线，可并存 |
| `PerfStats::cycles`（`Core::tick()` 递增） | 作为 Kanata 周期基准，与 `Model::run()` 中 `g_cycles` 对齐 |
| `tick()` + `commit()` 两阶段 | 阶段事件应挂在**指令实际进入该阶段的时刻**，而非逻辑"发射"时刻 |

> **注意**：ALU 的 `dispatch()` 在 `tick()` 中执行，`commit()` 才把指令推入 `pipe[0]`。Kanata 的 `EX` 阶段应在 FU `commit()` 时记录，否则时间轴会差 1 周期。

---

## 2. 总体架构

```
┌─────────────┐     record_event      ┌──────────────┐
│ Fetch       │ ────────────────────► │              │
│ Decode      │                       │ KanataTrace  │
│ Rename      │                       │   (单例)     │
│ Issue       │                       │              │
│ Execute     │                       └──────┬───────┘
│ Commit      │                              │ 周期末 drain
└─────────────┘                              ▼
                                      kanata.log ──► Konata Viewer
```

**核心设计**：新增 `KanataTrace` 单例，各模块只调用薄接口上报事件；**周期边界**在仿真循环末尾统一 `end_cycle()` 写 `C 1`，保证时序正确。

---

## 3. 新增文件与配置

### 3.1 文件布局

| 文件 | 职责 |
|------|------|
| `include/trace/kanata_trace.h` | 对外 API、阶段名常量 |
| `tools/trace/kanata_trace.cpp` | 格式化输出、周期缓冲、ID 分配 |
| `include/config.h`（扩展） | 开关与路径 |
| `modules/config.cpp`（扩展） | 默认值 |

### 3.2 配置项

在 `CoreConfig` 中增加：

```cpp
struct KanataTraceConfig {
    bool        enabled { false };
    std::string path { "kanata.log" };
    u64         start_cycle { 0 };   // 0 = 从头
    u64         end_cycle   { 0 };   // 0 = 无限制
    bool        emit_wakeup { true };
    bool        emit_stall  { true }; // lane=1 叠加层
};
```

在 `Model::build()` 或 `CommitUnit::build()` 中初始化（与 `InstrTrace::open` 类似），由配置控制是否开启。

---

## 4. 数据结构扩展

### 4.1 MicroOp 扩展字段

在 `include/decode/inst.h` 的 `MicroOp` 中增加：

```cpp
u64  kanata_id { UINT64_MAX };  // 文件内唯一 ID（I 命令第一参数）
u64  kanata_sim_id { 0 };       // 模拟器内部 ID（I 命令第二参数，可用 rid 编码）
bool kanata_open { false };     // 是否已发 I，避免重复
bool kanata_closed { false };   // 是否已发 R，避免重复 flush/retire
```

### 4.2 KanataTrace 内部状态

- `m_next_insn_id`：单调递增文件 ID
- `m_next_retire_id`：退休序号（`R` 的 `RETIRE_ID`）
- `m_rid_to_kanata_id`：用于 `W` 依赖（Phase 2）
- `m_active_stages[id][lane][stage]`：避免重复 `S`（或依赖 `E` 配对）

---

## 5. 阶段名映射

### 5.1 Phase 1 — MVP（对齐当前功能模型）

当前仿真未拆分 F0–F4 细粒度前端，先用**粗粒度阶段**：

| Kanata `STAGE_NAME` | 含义 | 挂钩位置 |
|--------------------|------|----------|
| `F` | Fetch / 进入前端 | `DecodeUnit::d0()` 创建 `MicroOp` 时 |
| `D` | Decode | `DecodeUnit::d1()` 写入 `decode_rename_q` 时 |
| `RN` | Rename | `RenameUnit::r1()` 设 `RENAMED` 时 |
| `DP` | Dispatch 到 IQ | `RenameUnit::dispatch()` 成功时 |
| `IQ` | 在 Issue Queue 等待 | `IssueUnit::async()` 入队时 |
| `IS` | Issue 选中 / 发射 | `IssueUnit::issue()` `dispatch` 成功时 |
| `EX` | 执行（ALU pipe[0]） | 各 FU `commit()` 推入 pipe 时 |
| `EX1` | 执行第二拍 | ALU `pipe[1]` 有效时 |
| `WB` | Writeback | `CommitUnit::inst_resolve()` 完成时 |
| `CM` | Commit / Retire | `CommitUnit::inst_commit()` 退休时 |

FU 细分（Phase 1 可选）：

- `BRU`、`AGU`、`LSU`、`FPU`、`VPU`、`TPU` 替代通用 `EX`
- Load/Store 拆：`AGU` → `LSQ` → `DTLB` → `DC` → `WB`

### 5.2 Phase 2 — 对齐设计文档微架构

当 Fetch 拆成 F0–F4、Rename 拆 R0–R2 后，扩展映射表即可：

```
F0 F1 F2 F3 F4 | D0 D1 | R0 R1 R2 | S0 S1 | E0 E1 E2 | M0 M1 | W0 C0
```

参考：`docs/RV-TCore.md` 第 1.3 节流水段划分。

---

## 6. KanataTrace API 设计

```cpp
class KanataTrace {
public:
    static KanataTrace& instance();

    bool open(const KanataTraceConfig& cfg);
    void close();

    bool enabled() const;
    bool tracing_cycle(u64 cycle) const;  // 窗口过滤

    // --- 指令生命周期 ---
    void on_inst_born(MInstPtr inst);     // 分配 ID，写 I + L
    void stage_begin(MInstPtr inst, u8 lane, const char* stage);
    void stage_end(MInstPtr inst, u8 lane, const char* stage);
    void on_wakeup(MInstPtr consumer, MInstPtr producer, u8 type = 0);
    void on_retire(MInstPtr inst);
    void on_flush(MInstPtr inst);

    // --- 周期控制（由 Core / Model 调用）---
    void begin_cycle(u64 cycle);
    void end_cycle();   // flush 缓冲 + 写 "C\t1\n"

private:
    void emit(const std::string& line);
    void drain_cycle_buffer();
    std::string format_label(MInstPtr inst);
};
```

### 6.1 L 命令格式

`TYPE=0` 时显示在 Konata 左侧面板：

```
L\t{id}\t0\t0x{pc:016x}: {inst_name}
```

---

## 7. 各模块挂钩点

### 7.1 Core / Model（周期边界）

**推荐**：在 `Model::run()` 中，`tick()` + `commit()` 之后调用 `end_cycle()`：

```cpp
void Model::run() {
    while (g_cycles < g_max_cycles) {
        g_cycles++;
        u64 cycle = g_cycles;
        KanataTrace::instance().begin_cycle(cycle);

        m_core->tick();
        m_core->commit();

        KanataTrace::instance().end_cycle();
        // ...
    }
}
```

### 7.2 DecodeUnit

| 事件 | 位置 |
|------|------|
| `on_inst_born` + `S F` | `d0()` 中 `new MicroOp()` 且 `decode()` 成功后 |
| `E F` / `S D` | `d1()` 推入 rename 队列前 |
| `E D` | `d1()` 写入 `decode_rename_q` 后 |
| `on_flush` | `flush()` 丢弃的 `MicroOp` |

> Store 指令拆成 STA/STD 两条 uop，各自独立 `kanata_id`。

### 7.3 RenameUnit

| 事件 | 位置 |
|------|------|
| `S RN` | `r1()` 设 `inst->state = RENAMED` |
| `E RN` / `S DP` | `r2()` `dispatch()` 成功 |
| `E DP` | `dispatch()` 写入 `rename_issue_q` 后 |
| `on_flush` | `flush()` 中回收/丢弃的指令 |

### 7.4 IssueUnit

| 事件 | 位置 |
|------|------|
| `S IQ` | `async()` 入 `m_int_q` 等 |
| `E IQ` / `S IS` | `issue()` `dispatch` 成功 |
| `S IQ` lane=1（stall） | `select()` 时指令 not ready（Phase 2） |
| `on_wakeup` | `wakeup()` 中 `src.ready` 从 false→true |
| `on_flush` | `flush()` 中 `valid=false` 的指令 |

### 7.5 执行单元（ALU / BRU / FPU / AGU / STD / LSU / Vector / Tile）

统一模式（以 ALU 为例）：

| 事件 | 位置 |
|------|------|
| `E IS` / `S EX` | `commit()`：`input_latch → pipe[0]` |
| `S EX1` | `shift()` 后 `pipe[1]` 有效 |
| `E EX` / `E EX1` | `shift()` 或完成执行 |
| `on_flush` | 各 `flush()` 设 `FLUSHED` 时 |

LSU 按 `LdStState` 转换记录 `LSQ` / `DTLB` / `DC` 等子阶段。

### 7.6 CommitUnit

| 事件 | 位置 |
|------|------|
| `S WB` | `inst_resolve()` 写回成功 |
| `E WB` / `S CM` | `inst_commit()` 开始处理 |
| `on_retire` | `inst_commit()` 设 `RETIRED` |
| `on_flush` | `flush()` 中标记 `FLUSHED` 的指令 |

### 7.7 Flush 集中处理

除各模块散点外，建议在 `ROB::flush()` 执行时，**遍历被无效的 ROB 条目**补发 `on_flush`：

```cpp
// rob.cpp flush() 中，invalid 条目前：
if (e.inst && !e.inst->kanata_closed)
    KanataTrace::instance().on_flush(e.inst);
```

IQ 里 `valid=false` 但未进 ROB 的指令，保留各模块 `on_flush` 调用。

---

## 8. 周期事件输出顺序

每个周期末 `end_cycle()` 按以下顺序 drain 缓冲：

1. `I`（新指令）
2. `L`（标签）
3. `S` / `E`（阶段起止）
4. `W`（依赖）
5. `R`（退休 / flush）
6. `C\t1`（推进 1 周期）

首周期写 `C=\t{start_cycle}\n`（在 `begin_cycle` 第一次调用时）。

### 8.1 示例输出

```
Kanata	0004
C=	0
I	0	0	0
L	0	0	0x1000: addi x1, x0, 1
S	0	0	F
S	0	0	D
C	1
E	0	0	F
S	0	0	RN
...
R	0	0	0
```

---

## 9. Stall 与依赖（Phase 2 增强）

### 9.1 Stall（Lane 叠加）

指令在 IQ 等待操作数时，使用 `lane=1` 叠加显示：

```
S	{id}	1	IQ_stall
...
E	{id}	1	IQ_stall
```

在 `IssueUnit::select()` 中，对 `!inst->ready()` 的候选指令发 `stall_begin`。

### 9.2 Wakeup 依赖（W 命令）

在 `IssueUnit::wakeup()` 中，当 consumer 的 `rs1/rs2` 变 ready：

```
W	{consumer_id}	{producer_id}	0
```

Producer 通过 `WakeupEvent.rid` 查 ROB entry → `inst->kanata_id`。

若执行阶段名含 `X`（如 `aX`），Konata 会自动画依赖箭头。

---

## 10. 与 InstrTrace 的关系

| 维度 | `InstrTrace` | `KanataTrace` |
|------|--------------|---------------|
| 粒度 | 仅退休 pc/result | 全流水线 |
| 输出 | 自定义文本 | Kanata 标准格式 |
| 开销 | 低 | 较高（默认关闭） |

建议：**保留 `InstrTrace`** 用于功能正确性回归；`KanataTrace` 用于微架构调试，默认 `enabled=false`。

---

## 11. 实施分期

### Phase 1（可验证 MVP）

- [ ] `KanataTrace` 基础设施 + 配置开关
- [ ] `MicroOp` 增加 `kanata_id` 等字段
- [ ] 挂钩：Decode → Rename → Issue → ALU → Commit
- [ ] `R` 退休 + 基本 flush
- [ ] `Model::run()` 调整周期边界
- [ ] 短程序跑通，Konata 能打开并看到流水线

### Phase 2（增强）

- [ ] LSU / FPU / Vector / Tile 细分阶段
- [ ] `W` 依赖 + IQ stall（lane=1）
- [ ] 周期窗口过滤（`start_cycle` / `end_cycle`）
- [ ] `ROB::flush` 集中补 flush

### Phase 3（对齐设计文档）

- [ ] Fetch 细粒度 F0–F4
- [ ] Rename R0–R2、Issue S0–S1 独立阶段
- [ ] 输出 `.gz` 压缩（Konata 支持）

---

## 12. 验证方案

1. **格式校验**：单元测试生成已知 trace，断言每行 Tab 列数与命令字符合法
2. **退休计数**：`Kanata R(TYPE=0)` 条数 == `PerfStats::commit.retired_insts`
3. **Flush 计数**：`R(TYPE=1)` + 退休 == 总 `I` 条数（仿真结束时）
4. **可视化**：用 Konata 打开，开启 "Hide flushed instruction"，检查 mispredict 场景是否有 flush 条带
5. **参考样本**：对比 [Konata 官方 sample](https://github.com/shioyadan/Konata/tree/master/docs) 的时间轴形态

---

## 13. 性能与注意事项

- **默认关闭**，仅调试时开启；长仿真可设 `end_cycle` 窗口
- 使用 `std::ostringstream` 缓冲，周期末批量写盘
- Store 拆分的 STA/STD 在 Konata 中显示为两条独立指令（符合实际 uop 流）
- 仿真器的 `ASyncQueue` 有 1 拍延迟：阶段事件必须挂在 **commit 相位**（如 ALU 进 pipe），否则 Kanata 时间轴会与真实模型差 1 周期

---

## 14. 关键代码锚点

| 模块 | 文件 | 函数 | 用途 |
|------|------|------|------|
| Core | `core/core.cpp` | `tick()` | 周期递增、模块调度 |
| Decode | `modules/frontend/decode_unit.cpp` | `d0()` / `d1()` | 指令诞生、F/D 阶段 |
| Rename | `modules/frontend/rename_unit.cpp` | `r1()` / `dispatch()` | RN/DP 阶段 |
| Issue | `modules/backend/issue_unit.cpp` | `async()` / `issue()` | IQ/IS 阶段 |
| ALU | `modules/backend/alu.cpp` | `commit()` / `tick()` | EX/EX1 阶段 |
| Commit | `modules/backend/commit_unit.cpp` | `inst_resolve()` / `inst_commit()` | WB/CM 阶段 |
| Model | `model.cpp` | `run()` | 周期边界控制 |

---

## 15. 参考资料

- [Konata GitHub](https://github.com/shioyadan/Konata)
- [Kanata Log Format 规范](https://github.com/shioyadan/Konata/blob/master/docs/kanata-log-format.md)
- [RV-TCore 微架构设计](RV-TCore.md)
- [gem5 + Konata 教程 (ASPLOS 2018)](https://github.com/shioyadan/Konata/wiki/gem5-konata.pdf)
