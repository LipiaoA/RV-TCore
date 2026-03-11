### 处理器流水线划分

##### 目标

**频率：3.5 GHz 左右（≈285 ps/周期）**
**流水段：18**

**核：乱序超标量核**

------

### 1. RV64 OoO 核流水线划分（18 stages）



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

**总计：18 stage**

------

### 2.取指单元

#### 2.1. 取指阶段划分

ICache + BPU 是频率瓶颈。

```
F0  PC select (BPU)
F1  ITLB
F2  ICache tag
F3  ICache data
F4  Align
```

#### 2.2.Fetch0

##### 2.2.1.目标

生成下一拍的 Fetch PC

##### 2.2.2.输入

1. 上一拍预测结果
2. Redirect重定向信息（来自 ROB miss predict / exception）
3. 顺序 PC

##### 2.2.3.任务

1. 确定PC，如果优先选择重定向，其次如果预测跳转，则使用预测目标地址，最后顺延

2. 向 BTB/predictor 发起请求

3. 向 ICache 发起请求

##### 2.2.3.输出

1. Fetch PC

#### 2.3.Fetch1

##### 2.3.1.任务

1. ICache Tag查询
2. 分支预测器的访问（BTB、BHT、RAS），拿到 btb_hit，pred_taken，pred_target

##### 2.3.2.输出

1. 预测信息 pred_info
2. ICache-Way
3. pc

#### 2.4.Fetch2

##### 2.4.1.任务

1. 从ICache data阵列读取 64B line
2. 读取 predecode bits (is_branch, is_jump, is_call, is_ret, target_offset)

##### 2.4.2.输出

1. 64 Bytes 的 cache line 数据
2. 预解码信息
3. pc
4. 预测信息

#### 2.5.Fetch3

##### 2.5.1.任务

1. 计算 fetch slot （pc 可能落在 cache line 的中间)
2. 取出连续4条指令
3. 根据预解码信息，找到第一个分支，根据预测结果拿到 next pc

##### 2.5.2.输出

1. fetch_bundle 
2. next_pc_pred

#### 2.6.Fetch4

##### 2.6.1.任务

1. 构建 Fetch Bundle
2. 写 Fetch Queue (FQ, deep 16-32 entries)
3. 写 Fetch Target Queue (FTQ, 16 entries)

```c++
struct FetchBundle {
    pc
    inst[4]
    valid_mask
    pred_taken
    pred_target
    br_mask
}
```

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



#### 2.7.输出

```c++
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



### 2 Rename 必须 2–3 stage

Rename 逻辑复杂度：

- RAT 读
- freelist allocate
- ROB allocate
- checkpoint

推荐：

```
R0  RAT read
R1  freelist allocate
R2  dispatch to IQ
```

否则 rename 扇出过大。

------

### 3 Issue Queue 必须拆 wakeup/select

这是 **OoO 最大关键路径**。

工业设计：

```
S0 wakeup
S1 select
```

原因：

```
tag broadcast
-> CAM match
-> priority select
-> grant
```

这一整条路径 3.5GHz 很难 1cycle。

------

### 4 Execute 必须拆两级

例如：

```
E0 register read
E1 ALU stage
E2 branch resolve
```

否则：

```
PRF read + ALU + bypass
```

会成为 critical path。

------

### 5 Load pipeline

高频设计通常：

```
AGU
DTLB
DCache
```

三拍：

```
E1 AGU
M0 DTLB
M1 DCache
```

------

### 3.Issue 核心结构

如果目标 **3.5 GHz + 4-issue**：

| 单元        | 规模    |
| ----------- | ------- |
| Decode      | 4       |
| Rename      | 4       |
| Issue Queue | 32–48   |
| ROB         | 160–224 |
| PRF         | 128–192 |
| Load Queue  | 32      |
| Store Queue | 32      |

------

### 4.整体 pipeline 

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

------

### 5.为什么要这么深？

因为 **3.5 GHz = 285ps**

在现代工艺中：

| 单元       | 延迟      |
| ---------- | --------- |
| CAM match  | 200–300ps |
| SRAM read  | 300–500ps |
| bypass mux | 200ps     |
| ALU        | 200–300ps |

所以必须：

- **ICache拆**
- **rename拆**
- **issue拆**
- **execute拆**

否则时序过不了。

------

### 6.和 BOOM 的区别

Berkeley Out-of-Order Machine (BOOM) 的流水线大约：

```
~12–14 stage
```

目标频率通常：

```
1.5–2 GHz
```

要到 **3.5 GHz** 必须：

- deeper frontend
- split rename
- split issue
- split execute

------

### 7.现实工业级 stage 数

实际 CPU：

| CPU                             | pipeline |
| ------------------------------- | -------- |
| Intel Skylake microarchitecture | ~19      |
| AMD Zen 3 microarchitecture     | ~19      |
| ARM Cortex-A76                  | ~13–15   |



**RV OoO 3.5GHz**

```
4-issue
18 stage
192 ROB
48 IQ
```

这是一个**比较现实的高频 OoO 核规模**。

------



下一步：

- ROB / PRF / Issue Queue 结构
- bypass 网络
- Load/Store Queue
- 分支预测结构

**工业 CPU 设计文档级别的结构图**。