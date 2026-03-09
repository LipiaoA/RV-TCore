### 1.各部件设计

#### 1.1.取指单元

取指单元划分为5各阶段，分别是F0到F4：

F0：确定需要取指的PC并做分支预测

F1：从 F0 的预测结果确定要请求哪条 cache line

F2：检查ICache是否有 Ready line，若有则读入

F3：指令解码准备（拼接、预解码）从上一阶读到的数据中组装k_fetch_width条指令。

F4：将 F3 生成的 packet 放入 dispatch 队列



#### 1.2.L1-ICache设计 



C++实现一个ICache模型,考虑DCache，或者L2 Cache等复用情况，支持可配置n路组相联，Cache Line大小可配置，支持每周期取多条指令，支持虚拟地址取指，使用TLB实现，非阻塞取指，Tag与Data分离，提高并行访问能力，替换策略使用LRU策略。



在Cache Miss 填充 I-Cache 时，需要做预解码，将解码信息缓存在 I-Cahce中。

I-Cache Line 设计大小：64B

Fetch Width：4



#### 1.3.分支预测

##### 1.3.1.方案一

**跳转方向预测**：PHT使用两位饱和计数器，BHT中表项采用N bits存储分支跳转历史记录，分支历史寄存器表（BHT）采用PC经过hash运算后访问内容，读取到BHR值后组合PC低n位，作为地址访问PHT表，该预测器作为local 分支预测器，针对每个PC自身做预测。使用一个GHR存储全局跳转指令分支跳转记录，补充另一个PHT存储分支预测器状态，作为global分支历史预器。最后使用一个两位饱和计数器在全局分支历史预测与local分支预测器之间做出结果预测选择。

**目标地址预测**：

分直接跳转类型指令，设计一个组相联BTB；针对call/ret，设计一个RAS缓存；对其它间接跳转指令设计一个基于BHT，查到HBR后结合pc通过hash运算索引目标地址缓存，这样一个BTB。



##### 1.3.2.方案二

TAGE-SC-L 预测方法

------



### 3. 前端设计（Front-end）

------

#### 3.1 Fetch 结构

##### 主要模块

- Fetch PC Gen
- BTB
- BPD（Branch Predictor）
- ICache
- Fetch Buffer（FTQ）



| 参数             | 推荐范围     |
| ---------------- | ------------ |
| Fetch Width      | 4            |
| Fetch Queue 深度 | 8~32 entries |
| BTB entries      | 64~512       |
| BHT entries      | 1K~8K        |

------

#### 3.2. 预测结构

- BTB：预测跳转目标
- BPD（GShare 或 TAGE）
- RAS
- FTQ（Fetch Target Queue）

#### 3.3.RTL 关键设计

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



### 4. Decode 设计

------

#### 4.1 解码输出信号

- fu_type
- src/dst logical reg
- immediate
- branch type
- memory op type

BOOM 将复杂指令拆分为 uop。

------

#### 4.2 RTL 关键点

- 解码宽度 = Fetch 宽度
- 输出进入 Rename Queue
- 避免 decode 成为关键路径

建议：

- 使用预解码字段（I$ 存部分字段）
- 或在 F2 阶段提前提取 opcode

------

### 5. Rename 设计

------

#### 5.1 Rename 结构

##### 组件：

- Map Table（RAT）
- Free List
- Busy Table
- Branch Checkpoint Table

------

### 5.2 Checkpoint 设计

**BOOM 做法：**

- 每个 unresolved branch 创建 rename snapshot
- 使用 shadow map 或 incremental log

**参数参考**

| 参数               | 推荐范围 |
| ------------------ | -------- |
| 物理寄存器数       | 128~192  |
| 最大分支 in-flight | 8~16     |

------

## 5.3 Rename 时序

Rename 高扇出模块：

- 每周期 4 读 2 写（4-wide 情况）
- RAT 多端口压力大

可优化：

- Banked RAT
- Shadow map

------

### 6. Dispatch 与 ROB

------

#### 6.1 ROB 结构

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

------

#### 6.2 Branch Mask 机制

BOOM 使用 branch mask：

- 每条指令记录它依赖的未决分支
- 分支错误时通过 mask 批量清除

这是 RTL 实现乱序恢复的核心技巧。

------

### 7. Issue Queue 设计

------

#### 7.1. Issue 类型

- Int Issue Queue
- Mem Issue Queue
- FP Issue Queue

分开有利于时序。

------

#### 7.2 Wakeup-Select 机制

##### Wakeup

- 执行单元广播 tag
- CAM 比较
- 设置 ready bit

##### Select

- oldest-first 或 age-based
- N 路发射选择

------

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

### 8. 执行单元

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

#### 8.3 LSU 设计（BOOM 风格）

------

##### 组件

- Load Queue
- Store Queue
- Store Data Buffer

------

##### 内存相关性处理

BOOM 支持：

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

------

### 9. Writeback 网络

------

#### 9.1 Tag 广播

- 执行单元输出 tag + data
- 更新 PRF
- 唤醒 IQ

广播网络复杂度 ~ O(N²)

------

#### 9.2 物理寄存器文件（PRF）

##### 定义：

- Unified PRF
- 多读多写端口

RTL 难点：

- 多端口 SRAM 实现
- 可能需使用 banking 或 duplication

------

### 10. Commit 阶段

------

#### 10.1 顺序提交

- 从 ROB 头部检查
- 提交最多 N 条（与宽度一致）
- 回收旧物理寄存器

------

#### 10.2 异常处理

- 精确异常
- flush 后端
- 恢复 rename snapshot

------

### 11. Flush 与恢复路径（必须重点设计）

------

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

### 12. 关键参数对面积与性能影响

| 参数         | 性能影响 | 硬件代价           |
| ------------ | -------- | ------------------ |
| ROB 增大     | ↑ ILP    | ↑ 面积             |
| IQ 增大      | ↑ 并行度 | ↑ 时序压力         |
| 发射宽度     | ↑ IPC    | 广播网络指数级复杂 |
| 分支预测精度 | ↑ IPC    | ↑ SRAM             |

------

### 13. RTL 设计优先级建议

建议路线：

##### 第一阶段

- 2-wide
- 64 ROB
- 32 IQ
- 128 PRF
- 简单 GShare

##### 第二阶段

- 加 branch mask
- 加 replay 机制
- 加 store-forward

##### 第三阶段

- 优化 wakeup-select
- 分 bank
- 优化时序

------

### 14. 工程难点

目标 RTL，可预见难点：

1. RAT 多端口时序
2. IQ CAM + select critical path
3. PRF 多端口实现
4. LSU 冲突检测
5. 分支恢复一致性







