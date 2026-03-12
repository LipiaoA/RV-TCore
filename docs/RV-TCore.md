### RV-TCore 各部件设计&流水线划分

### 1. 总体

#### 1.1 设计目标

目标频率：3.5 GHz（≈285 ps/cycle）
           核：Super Scalar Out-of-Order Core

#### 1.2 组成部件

取指单元（Instruction Fetch Unit）：指令取指

解码单元（Instruction Decode Unit）：指令预解码 & 解码

寄存器重命名单元（Register Rename Unit）：寄存器重命名，解决相关性问题

发射队列（Issue Queue）：暂存指令，等待操作数准备好发射执行

指令执行单元（Instruction Execute Unit）：执行指令，将结果写回

提交（Commit）：指令从ROB中退休

分支预测（Branch Predict Unit）：对分支指令方向和目标地址进行预测

缓存系统（Cache System）：分 L1、L2、L3 三级，L1 划分 ICache & DCache 。

旁路网络（Bypass）：

#### 1.3 流水段划分

| Stage | 名称              | 主要功能                   |
| ----- | ----------------- | -------------------------- |
| F0    | Next PC select    | BTB/BPU 预测选择 PC        |
| F1    | I-TLB lookup      | 指令 TLB                   |
| F2    | I-Cache tag       | ICache tag                 |
| F3    | I-Cache data      | 取出指令                   |
| F4    | Align / Predecode | 指令对齐 / boundary detect |
| D0    | Decode            | 指令 decode                |
| D1    | Decode2           | 生成 control / imm         |
| R0    | Rename map read   | RAT 读                     |
| R1    | Rename allocate   | 分配 ROB / PRF             |
| R2    | Dispatch          | 写入 Issue Queue           |
| S0    | Wakeup            | operand ready 检测         |
| S1    | Select            | issue select               |
| E0    | Reg read          | 读 PRF                     |
| E1    | Execute1          | ALU / AGU stage1           |
| E2    | Execute2          | ALU / branch / addr        |
| M0    | DTLB              | load/store 地址翻译        |
| M1    | DCache            | 数据 cache                 |
| W0    | Writeback         | 写回 PRF                   |
| C0    | Commit            | ROB commit                 |



```
           +------ Frontend ------+
F0  F1  F2  F3  F4
 |   |   |   |   |
BPU ITLB I$  I$ Align

           +------ Decode ------+
D0  D1
 |   |
Decode

           +------ Rename ------+
R0  R1  R2
 |   |   |
RAT Alloc Dispatch

           +------ Issue ------+
S0  S1
 |   |
Wake Select

           +------ Execute ------+
E0  E1  E2
 |   |   |
RF  ALU  Branch

           +------ Memory ------+
M0  M1
 |   |
DTLB D$

           +------ Commit ------+
W0  C0
```

### 2. 指令集

基于开源 RISC-V 指令集，实现 RV64G，即 IMAFD

------

### 3. Instruction Fetch Unit

> In the base RV32I ISA, there are four core instruction formats (R/I/S/U), as shown in Base instruction formats. All are a fixed 32 bits in length. The base ISA has IALIGN=32, meaning that instructions must be aligned on a four-byte boundary in memory. An instruction-address-misaligned exception is generated on a taken branch or unconditional jump if the target address is not IALIGN-bit aligned. This exception is reported on the branch or jump instruction, not on the target instruction. No instruction-address-misaligned exception is generated for a conditional branch that is not taken.

#### 3.1 结构

- Fetch PC Gen
- BPD（Branch Predictor）
- ICache
- Fetch Buffer（FTQ）

| 参数             | 范围         |
| ---------------- | ------------ |
| Fetch Width      | 4            |
| Fetch Queue 深度 | 8~32 entries |

------

#### 3.3 RTL 关键

FTQ 是恢复核心：

- 每个分支分配一个 FTQ entry
- 保存预测信息
- 分支错误时通过 FTQ 恢复

分支恢复路径：

```
Execute -> Detect Mispredict
        -> Flush backend
        -> Restore Rename Map
        -> Redirect PC
```

#### 3.4 Pipe Line

```
F0  PC select (BPU)
F1  ITLB
F2  ICache tag
F3  ICache data
F4  Align
```

#### 3.5 Fetch0

 **目标**

生成下一拍的 Fetch PC

 **输入**

1. 上一拍预测结果
2. Redirect重定向信息（来自 ROB miss predict / exception）
3. 顺序 PC

**任务**

1. 确定PC，如果优先选择重定向，其次如果预测跳转，则使用预测目标地址，最后顺延

2. 向 BTB/predictor 发起请求

3. 向 ICache 发起请求

**输出**

1. Fetch PC

#### 3.6 Fetch1

**任务**

1. ICache Tag查询
2. 分支预测器的访问（BTB、BHT、RAS），拿到 btb_hit，pred_taken，pred_target

**输出**

1. 预测信息 pred_info
2. ICache-Way
3. pc

#### 3.7 Fetch2

**任务**

1. 从ICache data阵列读取 64B line
2. 读取 predecode bits (is_branch, is_jump, is_call, is_ret, target_offset)

**输出**

1. 64 Bytes 的 cache line 数据
2. 预解码信息
3. pc
4. 预测信息

#### 3.8 Fetch3

**任务**

1. 计算 fetch slot （pc 可能落在 cache line 的中间)
2. 取出连续4条指令
3. 根据预解码信息，找到第一个分支，根据预测结果拿到 next pc

**输出**

1. fetch_bundle
2. next_pc_pred

#### 3.9 Fetch4

**任务**

1. 构建 Fetch Bundle
2. 写 Fetch Queue (FQ, deep 16-32 entries)
3. 写 Fetch Target Queue (FTQ, 16 entries)

FTQ 记录：

```c
struct FTQEntry {
    start_pc
    end_pc
    pred_taken
    pred_target
    bpu_state
}
```

FTQ 用于：

1. commit 更新 BPU
2. mispredict recovery

**输出**

```c
#define FETCH_WIDTH 4
struct FetchBundle
{
    uint64_t pc;
    uint32_t inst[FETCH_WIDTH];
    uint8_t  valid_mask;    // bit per instruction
    uint8_t  br_mask;       // 哪些是分支
    bool     pred_taken;
    uint64_t pred_target;
};
```



### 4. Instruction Decode Unit

------

#### 4.1 解码输出信号

- function unit type
- src/dst logical reg
- immediate
- branch type
- memory op type

> 注：BOOM 将复杂指令拆分为 uop。

#### 4.2 Pipe Line

阶段划分

```
D0  opcode/func3/func7 识别
D1  Control/imm
```

#### 4.3 Decode0

对指令进行解码，识别指令类型 得到：

```c
uop = {
  opcode
  rs1
  rs2
  rd
  imm_fmt
}
```

#### 4.4 Decode1

1. 生成所有控制信号
2. 根据指令类型生成立即数（I、S、B、U、J 5种指令类型）

生成控制信号：

```
alu_op
is_branch
is_load
is_store
is_mul
is_div
is_fpu
```

#### 4.5 Output

```
uop {
    pc
    opcode
    rs1
    rs2
    rd
    imm
    fu_type
    ctrl {
        alu_op
        branch_type
        is_load
        is_store
        is_mul
        is_div
    }
}
```

------

#### 4.2 RTL 关键点

- 解码宽度 = Fetch 宽度
- 输出进入 Rename Queue
- 避免 decode 成为关键路径

建议：

- 使用预解码字段（I$ 存部分字段）
- 或在 F2 阶段提前提取 opcode

------

### 5. Register Rename  Unit

------

#### 5.1 Rename 结构

使用统一的 PRF 进行寄存器重命名，包含以下组件：

**组件：**

- Mapping Table（RAT），即寄存器重命名映射表，记录了逻辑寄存器到对应物理寄存器的映射关系。
- Free List，记录当前空闲物理寄存器列表。
- Branch Checkpoint Table

当对一条指令进行重命名时，对于源寄存器，通过 RAT进行查询使用的物理寄存器，对于目的寄存器，查询 Free List 获取未被使用的物理寄存器编号。当需要从外部查看，当前处理器状态时，需要通过额外的 RAT 来记录所有退休状态的指令逻辑寄存器和物理寄存器状态信息。

对于 RAT 的实现方案，此处包含使用 基于SRAM表存储，即对于32逻辑寄存器则使用32个表项存储对应的物理寄存器编号即可。另外一种方案基于CAM，即表项个数等于逻辑寄存器个数，而表项内容存储逻辑寄存器编号。此处暂时考虑基于 SRAM 的 RAT，即 sRAT。

吐槽下，sRAT 在流水线中，只允许存在很少的四五个，而 基于CAM方式，流水线的 Checkpoint 则可以针对每条指令都保存一个，可达七八十个，差距也太大了点儿，后续再考虑。

#### 5.2 Checkpoint

**实现：**

- 每个 unresolved branch 创建 rename snapshot
- 使用 shadow map 或 incremental log

对于 Branch Checkpoint Table，该表用于存储分支指令预测时间点需要保存的 RAT信息，用于分支预测失败时做恢复。

#### 5.3 ROB 结构

- Circular buffer
- 每 entry 包含：
    - valid
    - busy
    - exception
    - branch mask
    - dest reg tag

**参数参考**

| 参数        | 推荐范围 |
| ----------- | -------- |
| ROB entries | 64~256   |

#### 5.4 Branch Mask 机制

使用 branch mask：

- 每条指令记录它依赖的未决分支
- 分支错误时通过 mask 批量清除

#### 5.5 Pipe Line

```
R0  RAT read
R1  freelist allocate
R2  dispatch to IQ
```

#### 5.6 Rename0

**任务**

1. 识别寄存器类型 has_rs1，has_rs2，has_rd
2. 生成 rename request
3. 特殊寄存器处理
4. 预计算 free list 需求

**输出**

Rename Request Bundle

#### 5.7 Rename1

**任务**

1. RAT lookup，读取每条指令的源寄存器
2. instr-cycle bypass，解决 Bundle 内部指令依赖问题，要读取到正确的寄存器
3. freelist alloc，分配物理寄存器（如果有的话）
4. RAT update，在更新RAT直线先保存一份旧的，然后再更新

**输出**

Rename1 uop

#### 5.8 Rename2

**任务**

1. ROB分配
2. ROB entry 保存
3. Issue Queue 分配
4. Load / Store Queue 分配
5. Busy Table 更新
6. 构建 Wakeup Tag

**输出**

Dispatch Bundle，送入 Issue Queue

------

### 6. Issue Queue 设计

#### 6.1 Issue  Queue 分类

- Int Issue Queue (ALU0-2, Branch Unit, Multiply / Divide Unit) ，32 entry
- Load & Store Instruction Issue Queue (连接 Load, Store Unit)，24 entry
- Float Point Instruction Issue Queue (连接 FPU)，20 entry



#### 6.2 Integer Issue Queue 结构

整数调度压力最大，所以必须拆结构。

推荐：

```
Integer Issue Queue = 32 entries
Banks = 4
Each bank = 8 entries
```

结构：

```
        Integer Issue Queue

        Bank0  Bank1  Bank2  Bank3
         8e     8e     8e     8e
          │      │      │      │
          +------+------+------+
                 Select Tree
                      │
        +-------------+-------------+
        │             │             │
      ALU0          ALU1          ALU2
```



#### 6.3 Memory Issue Queue 结构

Memory 指令较复杂（需要 LSQ），但 Issue Queue 可以简单。

```
entries = 24
bank = 2
```

#### 6.4 Floating Point Issue Queue 结构

```
entries = 20
bank = 2
```

结构：

```
FP Issue Queue

Bank0
Bank1

   │
 Select
   │
+----+----+---+
|         |   |
FP ALU  FMUL FDIV
```



#### 6.5 Issue Queue Entry

```c++
IssueEntry {
    bool valid;
    UOP uop;
    int src0_tag
    int src1_tag

    bool src0_ready
    bool src1_ready

    int dest_tag
    u64 fu_mask
    int rob_idx
    int age
}
```



> 注：machine width 表示每周期能够解码和重命名的指令个数，issue width 用来表示每周期能够发射的指令个数。

队列工作方式分压缩和非压缩两种，此处考虑采用压缩方式（当前CA实现，选什么都ok）。

#### 6.2 Pipe Line

Issue Queue 采用经典的 **Wakeup-Select 调度机制**。

该机制分为两个阶段：

```
Wakeup
Select
```

------

#### 6.3 Wakeup

当执行单元产生结果时，会在结果总线上 **广播目标寄存器 tag**。

Wakeup 过程包括：

1. 执行单元广播 dest tag
2. Issue Queue 中的 CAM 进行 tag 比较
3. 若匹配成功，则更新 operand value（若使用 payload），并将对应 src_ready bit 置位

Wakeup 可以分为两类：

1. Fast Wakeup，由执行阶段 bypass 网络产生，用于降低执行延迟

2. Slow Wakeup，由 writeback 阶段产生，用于补充未通过 bypass 捕捉的结果

------

#### 6.4 Select

Select 阶段从所有 ready 的指令中选择可以发射的指令。

选择策略通常为：**Oldest-first** 或 **Age-based scheduling**

调度逻辑需要同时考虑：指令 ready 状态、执行单元类型、发射端口可用性。

最终在每个周期最多选择 **issue width** 条指令发射到执行单元。

#### 7.3 RTL 难点

关键路径：

```
Tag Broadcast
   -> CAM Match
   -> Ready Update
   -> Priority Select
   -> Grant
```

当宽度=4，IQ=32 时：

- CAM 比较次数 = 32 × src_num × write_ports
- Select 树复杂度大

##### 可选优化

- 分 bank
- 分层 select
- 限制广播宽度

------

### 8. Instrcution Execute Unit

------

#### 8.1 整数单元

- 1~2 ALU
- 1 Mul/Div（多周期）

------

#### 8.2 Branch 单元

- EX 阶段解析
- 与预测比较
- 发出 redirect

关键：
branch resolution latency 直接影响 mispredict penalty。

------

#### 8.3 Load/Store Unit

------

##### 组件

- Load Queue
- Store Queue
- Store Data Buffer

------

##### 内存相关性处理

支持：

- Store-to-Load Forward
- 地址冲突检测
- Load replay

RTL 要点：

- 地址 ready 后才进行 conflict check
- Load 可能 replay

------

##### 参数参考

| 参数       | 推荐范围 |
| ---------- | -------- |
| LQ entries | 16~48    |
| SQ entries | 16~48    |

#### 8.4 配置

```
Integer
4 × ALU/Branch
1 × Mul/Div

Memory
2 × Load
2 × Store

FP
1 × FMA
1 × FP DIV

```

#### 8.5 ALU

ALU 通常 1-cycle latency

```
E0  operand read
E1  execute
WB  writeback
```

#### 8.6 BRU

BRU 1-cycle latency，独立出来的好处是可以提前算出结果，不独立可以省面积

#### 8.7 Mul/Div Unit

Mul 通常 3-4 个 cycle ，Div 通常 10-30 个 cycle

#### 8.8 Load/Store

```
L0 issue
L1 address generation
L2 TLB lookup
L3 cache access
L4 data return
```

#### 8.9 FPU

```
1 FP ALU  3-4 cycle
1 FP MUL  3-5 cycle
1 FP DIV  10-20 cycle
```

------

### 9. Commit 阶段

------

#### 9.1 顺序提交

- 从 ROB 头部检查
- 提交最多 N 条（与宽度一致）
- 回收旧物理寄存器

------

#### 9.2 异常处理

- 精确异常
- flush 后端
- 恢复 rename snapshot

------

### 10. Writeback 网络

------

#### 10.1 Tag 广播

- 执行单元输出 tag + data
- 更新 PRF
- 唤醒 IQ

广播网络复杂度 ~ O(N²)

------

#### 10.2 物理寄存器文件（PRF）

##### 定义：

- Unified PRF
- 多读多写端口

RTL 难点：

- 多端口 SRAM 实现
- 可能需使用 banking 或 duplication

### 11. Flush 与恢复路径

触发来源：

- 分支错误
- 异常
- TLB miss
- fence

恢复顺序：

1. 停止 fetch
2. 清空 IQ / LSQ
3. 恢复 rename
4. 清理 ROB
5. 重定向 PC

RTL 上建议：

- 使用 valid bit 批量清空
- 避免逐项删除

------

### 12. 分支预测

---

#### 12.1 方案一

**跳转方向预测**：PHT使用两位饱和计数器，BHT中表项采用N bits存储分支跳转历史记录，分支历史寄存器表（BHT）采用PC经过hash运算后访问内容，读取到BHR值后组合PC低n位，作为地址访问PHT表，该预测器作为local 分支预测器，针对每个PC自身做预测。使用一个GHR存储全局跳转指令分支跳转记录，补充另一个PHT存储分支预测器状态，作为global分支历史预器。最后使用一个两位饱和计数器在全局分支历史预测与local分支预测器之间做出结果预测选择。

**目标地址预测**：

直接跳转类型：使用组相联BTB

call/ret：RAS缓存

其它间接跳转指令：基于BHT，查到HBR后结合pc通过hash运算索引目标地址缓存。

#### 12.2 预测结构

- BTB：预测跳转目标
- BPD（GShare 或 TAGE）
- RAS
- FTQ（Fetch Target Queue）

| 参数        | 范围   |
| ----------- | ------ |
| BTB entries | 64~512 |
| BHT entries | 1K~8K  |

------

#### 12.2 方案二

TAGE-SC-L 预测方法

---

### 13. 缓存系统

#### 13.1  L1-ICache 设计

在Cache Miss 填充 I-Cache 时，需要做预解码，将解码信息缓存在 I-Cache中。

I-Cache Line 设计大小：64B

Fetch Width：4



### 12. 关键参数对面积与性能影响

| 参数         | 性能影响 | 硬件代价           |
| ------------ | -------- | ------------------ |
| ROB 增大     | ↑ ILP    | ↑ 面积             |
| IQ 增大      | ↑ 并行度 | ↑ 时序压力         |
| 发射宽度     | ↑ IPC    | 广播网络指数级复杂 |
| 分支预测精度 | ↑ IPC    | ↑ SRAM             |

------

### 14. 工程难点

目标 RTL，可预见难点：

1. RAT 多端口时序
2. IQ CAM + select critical path
3. PRF 多端口实现
4. LSU 冲突检测
5. 分支恢复一致性
