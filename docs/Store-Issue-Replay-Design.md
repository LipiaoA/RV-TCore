# Store 单 IQ 条目、执行接收握手与 Replay 设计

## 1. 目标

当前一条 Store 在 Rename 阶段被拆成 STA、STD 两个 `MicroOp`，并占用两个
Issue Queue（IQ）槽位。本文档定义以下改造方案：

1. 一条 Store 在 ROB、SQ、Rename→Issue 队列和 IQ 中均只保留一个
   `MicroOp`。
2. STA 和 STD 独立参与 ready 判断、仲裁、dispatch 和 replay。
3. dispatch 成功只表示请求进入执行单元输入锁存，不表示 IQ 可以释放。
4. AGU/STD 从 bypass 或 regfile 成功取得所需操作数后，向 IssueUnit 返回
   ACCEPT 反馈。
5. Store 的 STA、STD 均 ACCEPT 后，整体状态进入 `FU_ACTIVE`，IQ 才删除该
   entry。
6. 读取失败只 replay 对应子操作，已经 ACCEPT 的另一半保持不变。
7. 支持晚到 replay：IQ entry 已删除时能够重新插入，并用 generation 丢弃
   旧执行请求。

该方案同时统一普通指令的 IQ 生命周期：

```text
RENAMED/REPLAY
  -> dispatch pending
  -> FU_ACTIVE（执行单元成功取得操作数，IQ 删除）
  -> ISSUED（执行完成并送入完成队列）
  -> EXECUTED（Commit 已处理结果）
  -> COMMITTED
```

## 2. 实现约束

- 保持项目现有 C++17 风格。
- 类型使用 PascalCase，成员变量使用 `m_` 前缀，普通函数使用 snake_case。
- 不继续扩散现有 `EmitSrcRead` 风格；新增函数统一使用 snake_case。
- 单个函数不超过 80 行。超过时必须按“候选生成、dispatch、反馈处理、队列
  清理”等职责拆分。
- Store 在 IQ 中始终保持 `InstType::STORE`，禁止临时修改共享
  `MicroOp::type`。
- STA 固定使用 `rs1`（地址基址），STD 固定使用 `rs2`（Store data）。
- 同一 Store 可以在同周期生成 STA、STD 两个候选；每个候选分别消耗一个
  LS issue slot。
- flush 优先级高于 replay，replay 优先级高于 accept。

## 3. 当前实现中的关键问题

### 3.1 Rename 创建两个对象

`modules/frontend/rename_unit.cpp` 当前复制一个 STA 对象，并把原 Store 对象
修改成 STD：

```cpp
if (inst->type == InstType::STORE) {
    MInstPtr inst2 = new MicroOp();
    *inst2 = *inst;
    inst2->type = InstType::STA;
    inst2->rs2.valid = false;
    dispatch(inst2);

    inst->type = InstType::STD;
    inst->rs1 = inst->rs2;
    inst->rs2.valid = false;
}
dispatch(inst);
```

这会占两个 IQ 槽，同时产生一个不在 ROB 中的 STA 对象。

### 3.2 Issue 在 dispatch 成功时过早删除

`modules/backend/issue_unit.cpp` 当前在 `ExecuteUnit::dispatch()` 返回成功后
立即设置 `ISSUED`。`update_next_ready()` 随后按 `ISSUED` 删除 IQ entry。
此时 AGU/STD 可能还没有从 bypass 或 regfile 取得数据，replay 到达时可能
已经找不到 IQ entry。

### 3.3 AGU/STD 读取失败后只丢弃流水项

AGU/STD 当前在 bypass 或 regfile 数据缺失时清空 `m_pipe[0]`，但没有向
IssueUnit 返回对应 dispatch 已失败的信息。延迟 IQ 删除后，如果没有反馈，
entry 会永久停留在 pending 状态。

### 3.4 `FU_ACTIVE` 已定义但未使用

`include/decode/inst.h` 已存在 `InstState::FU_ACTIVE`。本方案将其定义为：

> 指令所需执行请求已经被执行单元成功接收，执行单元已取得本次执行所需的
> 操作数，IQ 不再负责保存该指令。

## 4. 数据结构

### 4.1 Store 子操作状态

在 `include/decode/inst.h` 增加：

```cpp
enum class StorePart {
    NONE = 0,
    STA,
    STD,
};

enum class StorePartState {
    WAIT_ISSUE = 0,
    WAIT_ACCEPT,
    ACCEPTED,
};
```

`MicroOp` 增加：

```cpp
StorePartState sta_state { StorePartState::WAIT_ISSUE };
StorePartState std_state { StorePartState::WAIT_ISSUE };
u32 sta_generation { 0 };
u32 std_generation { 0 };
bool fu_pending { false };
```

语义：

- `WAIT_ISSUE`：尚未 dispatch，可以进入 select。
- `WAIT_ACCEPT`：已 dispatch 到 FU，等待 FU 确认读数，禁止重复 dispatch。
- `ACCEPTED`：FU 已经取得该子操作所需数据。
- generation 在 replay/flush 使旧 token 失效时递增。
- `fu_pending` 供非 Store 指令使用，避免普通指令在等待 ACCEPT 时重复
  dispatch。

不得继续增加 `sta_issued/std_issued/sta_accepted/std_accepted` 四个 bool。
两个三态字段能更明确地表达状态，避免非法组合。

### 4.2 Issue 请求

在 `include/module_comm.h` 增加：

```cpp
struct IssueRequest {
    MInstPtr inst { nullptr };
    StorePart part { StorePart::NONE };
    u32 generation { 0 };
};
```

`StorePart::NONE` 表示普通指令。该结构只存在于 select buffer 和 FU pipeline，
不占 IQ 槽。

将 `FUStage` 调整为保存 `IssueRequest`，或者增加独立 `IssueStage`。推荐直接
调整：

```cpp
struct FUStage {
    bool valid { false };
    IssueRequest request {};
};
```

执行单元内部通过 `request.inst` 访问指令，通过 `request.part` 区分 STA/STD。

### 4.3 FU 接收反馈

在 `include/module_comm.h` 增加：

```cpp
enum class FuAcceptResult {
    ACCEPTED = 0,
    REPLAY,
};

struct FuAcceptEvent {
    bool valid { false };
    MInstPtr inst { nullptr };
    StorePart part { StorePart::NONE };
    FuAcceptResult result { FuAcceptResult::ACCEPTED };
    u32 generation { 0 };
};
```

在 `ModuleCommQueues` 增加一个异步队列：

```cpp
ASyncQueue<FuAcceptEvent>* fu_accept_q { nullptr };
```

在 `IssueDeps`、`ExecDeps`、`AGUDeps`、`STDDeps` 以及普通 FU deps 中传递该
队列。所有 FU 只生产反馈，IssueUnit 是子状态和 `FU_ACTIVE` 的唯一写入者。

如果第一版只先完成 Store，可以仅向 AGU/STD 传递反馈队列，但最终应将普通
FU 统一到相同生命周期，避免 IQ 存在两套删除语义。

### 4.4 Replay 回灌请求

晚到 replay 仍使用 `FuAcceptEvent{result = REPLAY}`，不增加第二套 replay
消息。IssueUnit 按 `rid` 判断 entry 是否仍在 `m_ls_q`：

- entry 仍在 IQ：只更新子状态。
- entry 已删除：重新插入 `m_ls_q`。

禁止按裸指针是否相等以外的临时 clone 判断；Store 全生命周期只有一个
`MicroOp`。

## 5. Rename 行为

修改 `modules/frontend/rename_unit.cpp`：

1. 删除 Store 的 `new MicroOp()` 和 STA/STD 双 dispatch。
2. 保留原始 `InstType::STORE`。
3. 保留 `rs1` 为地址基址，保留 `rs2` 为 Store data。
4. 初始化 STA/STD 状态和 generation。
5. 只调用一次 `dispatch(inst)`。
6. `can_dispatch()` 对 Store 的 Rename→Issue 容量需求从 2 改成 1。
7. ROB、SQ 仍各分配一次，`SQEntry::uop` 始终指向该 STORE 对象。

预期伪代码：

```cpp
m_lsu.allocate(inst);

if (inst->type == InstType::STORE) {
    inst->sta_state = StorePartState::WAIT_ISSUE;
    inst->std_state = StorePartState::WAIT_ISSUE;
    inst->sta_generation = 0;
    inst->std_generation = 0;
}

dispatch(inst);
```

## 6. Issue 候选与仲裁

### 6.1 Select buffer

将 `IssueUnit::m_select_buffer` 从：

```cpp
std::vector<MInstPtr>
```

改为：

```cpp
std::vector<IssueRequest>
```

候选也使用 `IssueRequest`，避免修改共享 `inst->type`。

### 6.2 Ready 判断

Store 不使用要求 `rs1/rs2` 同时 ready 的 `MicroOp::ready()`：

```cpp
bool IssueUnit::store_part_ready(
    const MInstPtr inst,
    StorePart part) const
{
    if (part == StorePart::STA) {
        return inst->sta_state == StorePartState::WAIT_ISSUE &&
               (!inst->rs1.valid || inst->rs1.ready);
    }
    return inst->std_state == StorePartState::WAIT_ISSUE &&
           (!inst->rs2.valid || inst->rs2.ready);
}
```

普通指令候选要求：

```text
state 为 RENAMED 或 REPLAY
&& !fu_pending
&& inst->ready()
```

Store 候选要求：

```text
state 为 RENAMED 或 REPLAY
&& 对应 part 为 WAIT_ISSUE
&& 对应源操作数 ready
```

### 6.3 同周期双发

一条 Store 可以同时产生：

```cpp
{inst, StorePart::STA, inst->sta_generation}
{inst, StorePart::STD, inst->std_generation}
```

每个请求分别消耗一个 `m_ls_issue_width`。同 ROB age 的两个请求默认 STA
优先，以便尽早暴露地址和内存顺序冲突；如果 LS issue width 足够，二者可在
同周期发射。

建议把 `select()` 拆为以下小函数：

```cpp
void collect_candidates(std::vector<IssueRequest>& candidates);
void collect_store_candidates(
    const MInstPtr inst,
    std::vector<IssueRequest>& candidates);
void select_by_type(
    const std::vector<IssueRequest>& candidates,
    InstTypeClass type,
    size_t width);
```

每个函数控制在 80 行以内。

## 7. Dispatch 与寄存器读取

`ExecuteUnit::dispatch` 修改为：

```cpp
bool dispatch(const IssueRequest& request);
```

路由规则：

```text
part == STA -> AGU
part == STD -> STD
part == NONE -> 按 inst->type 路由普通 FU
```

dispatch 成功后，IssueUnit 只做：

```cpp
if (request.part == StorePart::STA)
    inst->sta_state = StorePartState::WAIT_ACCEPT;
else if (request.part == StorePart::STD)
    inst->std_state = StorePartState::WAIT_ACCEPT;
else
    inst->fu_pending = true;
```

此时不得设置 `ISSUED`、`FU_ACTIVE` 或删除 IQ entry。

PRF 读取请求只在 dispatch 成功后发送，且 Store 按 part 发送：

```text
STA -> EmitSrcRead(inst, inst->rs1, 1)
STD -> EmitSrcRead(inst, inst->rs2, 2)
```

dispatch 失败不得发送新的 PRF read，并保持对应状态为 `WAIT_ISSUE`。

## 8. AGU/STD 接收判定

### 8.1 STA

AGU 对 Store STA 只检查 `rs1`：

- 无有效 `rs1`：直接 ACCEPT。
- bypass hint 且找到匹配 bypass：写入 `rs1.val` 后 ACCEPT。
- 非 bypass 且找到匹配 regfile response：写入 `rs1.val` 后 ACCEPT。
- 本周期需要的数据未找到：发送 REPLAY，丢弃本地 pipeline token。

不得检查 Store 的 `rs2`。

### 8.2 STD

STD 只检查 `rs2`：

- 无有效 `rs2`：直接 ACCEPT。
- bypass hint 且找到匹配 bypass：写入 `rs2.val` 后 ACCEPT。
- 非 bypass 且找到匹配 regfile response：写入 `rs2.val` 后 ACCEPT。
- 本周期需要的数据未找到：发送 REPLAY，丢弃本地 pipeline token。

STD E0 固定使用：

```cpp
sq->data = inst->rs2.valid ? inst->rs2.val : inst->data;
```

删除兼容旧 STD 拆分对象的 `rs1` fallback。

### 8.3 ACCEPT 的准确时点

ACCEPT 必须在数据已写入 `Operand::val`、且 token 即将离开 operand-read
阶段时发送。不能在以下时点发送：

- `dispatch()` 写入 input latch 时；
- 仅看到 `src.ready == true` 时；
- 只发出 PRF read request 时。

每个 request 只允许发送一次 ACCEPT 或一次 REPLAY。

建议 AGU/STD 内部增加不超过 80 行的小函数：

```cpp
bool capture_store_operand(FUStage& stage);
bool capture_from_bypass(Operand& src);
bool capture_from_regfile(Operand& src);
void send_accept(const IssueRequest& request);
void send_replay(const IssueRequest& request);
```

## 9. IssueUnit 反馈处理

IssueUnit 每拍在 IQ 清理之前消费全部 `fu_accept_q`：

```text
flush
-> async
-> wakeup
-> handle_fu_feedback
-> cleanup_queues
-> issue
-> select
```

### 9.1 ACCEPT

先校验：

- `event.valid`；
- `event.inst != nullptr`；
- 指令未被 flush；
- event generation 与对应 part 当前 generation 相同；
- 当前 part 状态为 `WAIT_ACCEPT`。

然后：

```cpp
part_state = StorePartState::ACCEPTED;

if (sta_state == StorePartState::ACCEPTED &&
    std_state == StorePartState::ACCEPTED) {
    inst->state = InstState::FU_ACTIVE;
}
```

普通指令 ACCEPT：

```cpp
inst->fu_pending = false;
inst->state = InstState::FU_ACTIVE;
```

### 9.2 REPLAY

STA replay：

```text
sta_generation++
sta_state = WAIT_ISSUE
state = REPLAY
next_valid = false
SQ.addr_ready = false
STD 状态保持不变
```

STD replay：

```text
std_generation++
std_state = WAIT_ISSUE
state = REPLAY
next_valid = false
SQ.data_ready = false
STA 状态保持不变
```

普通指令 replay：

```text
fu_pending = false
state = REPLAY
```

如果 Store replay 时 `inst` 已不在 `m_ls_q`，将同一个 `MInstPtr` 重新插入。
插入前按指针或 `rid + store_id` 去重。

### 9.3 反馈优先级

同一拍按以下顺序处理：

1. flush：直接使 token/entry 失效；
2. replay：generation 递增，使旧 ACCEPT 无效；
3. accept：只接受 generation 匹配事件。

实现上可以先收集 replay，再处理 accept，或者为 feedback 排序。不得依赖
vector 中偶然的事件顺序。

## 10. IQ 清理

删除当前按 `ISSUED + next_valid` 延迟删除的规则。统一改为只删除
`FU_ACTIVE`：

```cpp
void IssueUnit::cleanup_queue(std::vector<MInstPtr>& queue)
{
    std::vector<MInstPtr> retained;
    retained.reserve(queue.size());

    for (auto* inst : queue) {
        if (!inst || !inst->valid)
            continue;
        if (inst->state != InstState::FU_ACTIVE)
            retained.emplace_back(inst);
    }

    queue.swap(retained);
}
```

Store 只有 STA、STD 都 ACCEPT 后才进入 `FU_ACTIVE`。部分 ACCEPT 的 Store
必须继续保留在 `m_ls_q`。

`next_valid` 如果没有其他使用点，应在本改造完成后删除；如果仍被其他模块
使用，先停止 IssueUnit 对它的依赖，不做无关清理。

## 11. 执行完成与 Commit

IQ 删除时指令状态为 `FU_ACTIVE`。当前 Commit 只接受 `ISSUED`，因此每个
完成生产者在送入 `inst_commit_q` 前必须设置：

```cpp
inst->state = InstState::ISSUED;
```

普通 ALU/FPU/BRU/Vector/Tile 在 commit candidate 就绪时设置。Store 在 LSU
确认：

```text
addr_ready
&& data_ready
&& !sta_completed
```

并把 Store uop 写入完成队列时设置。

不要在 STA 或 STD 单独 E0 完成时设置 `ISSUED`。

## 12. Flush 与旧 token

flush 必须同时处理：

- IQ entry；
- `m_select_buffer` 中的 `IssueRequest`；
- FU input latch；
- FU pipeline stage；
- 尚未消费的 `FuAcceptEvent`。

FU feedback 带 generation，IssueUnit 还必须检查 `inst->valid` 和 flush 的
ROB 范围。flush 后到达的 ACCEPT/REPLAY 均忽略。

Branch mispredict 保留老指令时，不得清除老 Store 已 ACCEPT 的 part；年轻
Store 整体失效。

## 13. 统计口径

建议明确区分：

- `selected_insts`：逻辑指令候选数，Store 同周期双候选只算一条时需要额外
  去重。
- `issued_uops`：成功 dispatch 的执行请求数，STA/STD 分别计数。
- `fu_accepted_uops`：FU 成功取得操作数的请求数。
- `replayed_uops`：FU 返回 REPLAY 的请求数。

为了减少本次范围，可以保留现有 `issued_insts` 并按请求计数，但必须在代码
注释中注明其实际是 uop/request 计数。

## 14. 逐文件修改清单

### 核心类型和连线

- `include/decode/inst.h`
  - 增加 `StorePart`、`StorePartState`。
  - 增加两个子状态、generation 和普通指令 pending 状态。
- `include/module_comm.h`
  - 增加 `IssueRequest`、`FuAcceptResult`、`FuAcceptEvent`。
  - 调整 `FUStage`。
  - 增加 `fu_accept_q` 及各 deps 指针。
- Core/Model 的模块创建与连线代码
  - 创建 `ASyncQueue<FuAcceptEvent>`。
  - 传给 IssueUnit、ExecuteUnit 和各 FU。
  - 在正确的 commit 阶段提交该异步队列。

### Rename

- `modules/frontend/rename_unit.cpp`
  - Store 只 dispatch 一次。
  - Store Rename→Issue 容量需求改为 1。
  - 保留 rs1/rs2 原始含义。

### Issue

- `include/issue/issue_unit.h`
  - `m_select_buffer` 改为 `IssueRequest`。
  - 增加候选生成、Store ready、反馈处理、IQ 去重/回灌和清理函数。
- `modules/backend/issue_unit.cpp`
  - Store 拆成两个临时候选。
  - dispatch 后进入 pending，不设置 `ISSUED`。
  - 消费 ACCEPT/REPLAY。
  - `FU_ACTIVE` 后清理 IQ。

### Execute/FU

- `include/execute/execute_unit.h`
- `modules/backend/execute_unit.cpp`
  - dispatch 参数改为 `IssueRequest`。
  - 按 `request.part` 路由 STA/STD。
- `include/execute/agu.h`
- `modules/backend/lsu/agu.cpp`
  - pipeline 保存 request。
  - STA 只读取 rs1。
  - 成功/失败发送反馈。
- `include/execute/stx.h`
- `modules/backend/stx.cpp`
  - pipeline 保存 request。
  - STD 只读取 rs2。
  - 成功/失败发送反馈。
- 其他 FU
  - 如果同一提交统一普通指令生命周期，增加 ACCEPT/REPLAY 反馈。
  - 如分阶段实现，第一阶段保留非 Store 原行为，但 IQ 清理函数必须明确区分
    Store 和非 Store；最终合并前应统一。

### LSU/Commit

- `modules/backend/lsu/lsu.cpp`
  - STA replay 清 `addr_ready`。
  - STD replay 清 `data_ready`。
  - Store 完成送队列前设置 `ISSUED`。
- 其他执行完成生产者
  - 完成送队列前设置 `ISSUED`。

## 15. 测试驱动顺序

必须先写测试并确认失败，再写生产实现。

### 15.1 Rename 测试

修改 `tests/module/rename_test.cpp`：

1. Store 只向 `rename_issue_q` 写一个 entry。
2. entry 类型仍为 `STORE`。
3. `rs1` 保留地址源，`rs2` 保留数据源。
4. ROB 和 SQ 各只有一个对应 entry。
5. Rename 队列只有一个空位时 Store 仍可 dispatch。

### 15.2 Issue Store 候选测试

修改 `tests/module/issue_test.cpp`：

1. 仅 rs1 ready 时只产生 STA 请求。
2. 仅 rs2 ready 时只产生 STD 请求。
3. 两者 ready 且 LS width 为 2 时同周期产生两个请求。
4. LS width 为 1 时 STA 优先，下一周期仍可发 STD。
5. dispatch 失败时 part 保持 `WAIT_ISSUE`，不产生 PRF read。
6. dispatch 成功时 part 为 `WAIT_ACCEPT`，IQ entry 仍保留。

### 15.3 ACCEPT 与 IQ 清理测试

1. 只有 STA ACCEPT：STA 为 `ACCEPTED`，Store 仍在 IQ。
2. 只有 STD ACCEPT：STD 为 `ACCEPTED`，Store 仍在 IQ。
3. 两者都 ACCEPT：整体进入 `FU_ACTIVE`。
4. 下一次清理后 Store 才离开 IQ。
5. dispatch 成功但无 ACCEPT：无论等待多少拍都不能删除 IQ entry。

### 15.4 Replay 测试

1. STA `WAIT_ACCEPT -> REPLAY`：只复位 STA，STD ACCEPT 保持。
2. STD `WAIT_ACCEPT -> REPLAY`：只复位 STD，STA ACCEPT 保持。
3. entry 已从 IQ 删除后收到晚到 replay：同一指针重新插入一次。
4. 重复 replay 不产生重复 IQ entry。
5. generation 不匹配的旧 ACCEPT 被忽略。
6. replay 后旧 FU token 不能更新 SQ。

### 15.5 AGU/STD 测试

修改 `tests/module/execute_test.cpp` 和 `tests/module/lsu_test.cpp`：

1. AGU 从 bypass 取得 rs1 后发送 STA ACCEPT。
2. AGU 从 regfile 取得 rs1 后发送 STA ACCEPT。
3. AGU 未取得 rs1 时发送 STA REPLAY。
4. STD 从 bypass 取得 rs2 后发送 STD ACCEPT。
5. STD 从 regfile 取得 rs2 后发送 STD ACCEPT。
6. STD 未取得 rs2 时发送 STD REPLAY。
7. STD 不等待 rs1，AGU 不等待 rs2。
8. STA/STD generation 过期时不更新 SQ。

### 15.6 Flush 和完成测试

1. flush 清除 IQ、select buffer、FU token 和反馈。
2. flush 后晚到 ACCEPT 被忽略。
3. Store 两个 part ACCEPT 后为 `FU_ACTIVE`，但不能进入 Commit。
4. SQ 地址和数据均完成后设置 `ISSUED`，Commit 正常处理。

## 16. 建议实施批次

按以下顺序实现，每批保持可编译：

1. 增加类型、请求、反馈队列及模块连线。
2. Rename 改为单 Store entry，并完成 Rename 测试。
3. Issue 使用 `IssueRequest`，实现 Store 独立候选和 pending 状态。
4. AGU/STD 使用 request 并返回 ACCEPT/REPLAY。
5. Issue 消费反馈，在双 ACCEPT 后按 `FU_ACTIVE` 清理。
6. 实现 generation、晚到 replay 回灌和 SQ 局部失效。
7. 统一普通 FU 的 ACCEPT 生命周期。
8. 调整执行完成端设置 `ISSUED`。
9. 完成 flush、统计和全量回归。

不应一次性改完所有文件后再补测试。

## 17. 验收命令

项目支持的 Linux 工具链可用后执行：

```sh
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target \
  issue_test rename_test execute_test lsu_test commit_test -j4
./bin/rename_test
./bin/issue_test
./bin/execute_test
./bin/lsu_test
./bin/commit_test
ctest --test-dir build --output-on-failure
```

同时检查：

```sh
git diff --check
```

当前创建本文档时的环境限制：Windows PowerShell 中没有 `cmake`，当前 WSL
也没有 cmake/make/C++ 编译器，Docker daemon 未启动。因此后续实现模型必须
先确认可用构建环境，再开始 TDD 的 RED/GREEN 验证。

## 18. 完成定义

只有同时满足以下条件才可声明完成：

- 一条 Store 在 IQ 中始终只占一个槽。
- Store 不再创建独立 STA `MicroOp`。
- STA/STD 可独立 dispatch、ACCEPT 和 replay。
- dispatch 成功但 operand read 失败时，IQ entry 不丢失。
- 两个 part 均 ACCEPT 前，Store 不得从 IQ 删除。
- 晚到 replay 能回灌，且旧 generation 不污染 SQ。
- Store 完成和 Commit 状态转换正确。
- 新增和修改函数均不超过 80 行。
- 相关模块测试和全量 `ctest` 通过。
- `git diff --check` 无格式错误。
