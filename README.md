# RV-TCore

#### 1.简介

​	RV-TCore 是一个用 C++17 写的 RISC-V 微架构项目：一个周期精确（cycle-accurate）的超标量乱序核模型，外加一个指令级功能参考模型。周期模型按拍推进取指、译码、重命名、发射、执行和提交；功能模型按指令顺序执行同一份 ELF，在退休点对照架构状态。适合用来读一条乱序流水线是怎么接起来的，改微架构参数后看 IPC 和停顿从哪里来，以及给新模块做退休级检查。



#### 2.流水线

默认配置是 4 宽取指 / 译码 / 重命名 / 提交。整数发射宽度为 4，访存发射宽度为 2。

```text
取指 Fetch ── 译码 Decode ── 重命名 Rename ── 发射 Issue
                                                    │
                    ┌───────────────┬───────────────┼────────────┬──────────┐
                    ▼               ▼               ▼            ▼          ▼
                   ALU             BRU             LSU          FPU      VPU / TPU
                    │               │               │            │          │
                    └───────────────┴─────── 写回 / 唤醒 ────────┴──────────┘
                                            │
                                       ROB 顺序提交
```

一条指令大致经过这些结构：

| 阶段 | 做什么 |
| --- | --- |
| 取指 | 选下一 PC，查 ITLB / ICache，用分支预测切取指块，写入 Fetch Queue |
| 译码 | 识别 opcode、寄存器和立即数，标上功能单元类型 |
| 重命名 | 查 RAT，从空闲表分配物理寄存器，分配 ROB / 发射队列项，为未决分支做检查点 |
| 发射 | 按整数、访存、浮点、向量、Tile 分队列，操作数就绪后按年龄选择发射 |
| 执行 | ALU、分支、地址生成、Store Data、Load/Store、浮点、向量、Tile |
| 提交 | 从 ROB 头顺序退休，写回架构寄存器，回收旧物理寄存器；分支错误或异常时冲刷后端并恢复 RAT |

分支预测由 BIM、TAGE、BTB 和 RAS 组成。预测失败时冲刷后端、恢复重命名映射，并把取指重定向到正确 PC。

访存侧有 Load Queue、Store Queue、Store Buffer，以及 ITLB / DTLB。缓存为 L1 ICache、L1 DCache 和 L2，替换策略用 pseudo-LRU。



#### 3.默认微架构参数

参数集中在 `modules/config.cpp` 的 `TCoreConfig::init_config()`。下表是当前默认值。

| 参数 | 默认值 |
| --- | --- |
| 取指 / 译码 / 重命名 / 提交宽度 | 4 |
| ROB | 128 项 |
| 整数物理寄存器（PRF） | 192 |
| 整数发射队列 / 发射宽度 | 48 项 / 4 |
| ALU / BRU | 6 / 1 |
| 访存发射宽度 | 2 |
| AGU / Store Data | 4 / 2 |
| L1 ICache | 32 KiB，4 路，64 B 行 |
| L1 DCache | 64 KiB，4 路，64 B 行 |
| L2 | 512 KiB，8 路，64 B 行 |
| BTB | 256 组 × 4 路 |
| BIM | 16384 项，历史长度 16 |
| RAS | 64 |

浮点、向量、Tile 的执行单元已经挂在执行级上（2 个 FPU、1 个 VPU、1 个 TPU），对应发射宽度默认为 0。要跑这些路径，需要在配置里打开 `fp_issue_width`、`vec_issue_width`、`tile_issue_width`。



#### 4.指令与两个模型

译码表在 `include/isa/opcode.inc`，覆盖 RV64 整数、乘除、原子、单精度 / 双精度浮点。指令语义在 `isa/inst_exec.cpp`。当前取指按 4 字节定长指令处理。

向量和 Tile 处于第一阶段：有独立寄存器堆（VPRF / TPRF）、重命名（VRAT / TRAT）和执行骨架。向量侧目前按字节做简化的 VV / VX 运算；Tile 侧做简化的逐字节加或拷贝。`vl`、mask、向量访存、矩阵乘加还在设计笔记里，见 `docs/`。

仓库里有两个可执行文件：

| 程序 | 作用 |
| --- | --- |
| `bin/tcore` | 周期精确模型。加载 ELF，按拍跑核，退休时和功能模型逐条对照，结束时打印性能统计 |
| `bin/rv_func` | 只跑功能模型，用来单独看指令条数、PC 和 `a0`，也可打开指令 trace |

`tcore` 在提交到 `ecall`、退休结果与参考模型不一致，或周期数到达上限时停止。周期上限目前写死为 5000，在 `Model::build()` 里。栈放在 `0x80000000`，大小 4 MiB。



#### 5.目录

```text
.
├── main.cpp, model.cpp      周期精确仿真入口与主循环
├── include/                 公共头文件
├── core/                    Top 组装与 Core 的 tick / commit
├── modules/
│   ├── frontend/            取指、译码、重命名、分支预测
│   ├── backend/             发射、执行、ROB、提交、LSU、向量与 Tile
│   ├── memory/              内存、Cache、TLB
│   └── perf/                性能计数与 Top-Down
├── isa/                     译码与指令执行语义
├── func/                    功能参考模型与 rv_func
├── verify/                  退休指令对照
├── tools/                   日志、ELF 读取
├── util/
├── tests/                   单元测试与模块测试
└── docs/                    设计笔记
```



#### 6.构建

需要 CMake 3.16+ 和带 C++17 的编译器。工程在 Linux 上开发，ELF 加载使用 `mmap` ，环境：

```
uname -a
Linux lip-dev 6.8.0-136-generic #136-Ubuntu SMP PREEMPT_DYNAMIC Wed Jul  1 21:53:05 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux
```

编译：

```bash
mkdir build
cd build
cmake .. && make -j
```

产物在 `bin/tcore` 和 `bin/rv_func`。测试：

```bash
./bin/tcore -l 4 -f ./run/ctest
```

测试可执行文件在 `bin/tests/`，包括 cache、分支预测、译码、重命名、发射、执行、LSU、提交和向量寄存器堆。



**注：当前阶段不支持C扩展，不支持系统调用** **，因此编译器需要自行编译一份不带C扩展的编译器，同时也提供了自己的 `run/start.s` 用于链接，提供了一个可用于测试的 ELF文件 `./run/ctest`；**



#### 7.运行

日志写在**当前工作目录**。

```bash
./bin/tcore -l 4 -f path/to/prog.elf
./bin/tcore -h
```

| 选项 | 含义 |
| --- | --- |
| `-f, --file` | 要执行的 ELF，必填 |
| `-l, --log` | 日志级别 0–5。`Model::build()` 随后会把级别设回 3，并把 `system.log` 再挂上一次 |
| `-h, --help` | 帮助 |

跑完后当前目录会有：

- `system.log`：按周期打印的流水线日志
- `instr_trace.log`：每条退休指令的 PC 和结果
- 标准输出末尾的 `PerfStats` 和 `Topdown Results`（IPC、Retiring、Frontend Bound、Backend Bound、Bad Speculation）

只跑功能模型：

```bash
./bin/rv_func -f path/to/prog.elf
```

| 选项 | 含义 |
| --- | --- |
| `-f, --file` | ELF，必填 |
| `-n, --max-inst` | 最多执行的指令数，默认 1000000 |
| `-t, --trace` | 写出 `func_inst_trace.log` |
| `-l, --log` | 日志级别，写入 `func.log` |

结束时打印 `inst_cnt`、PC 和 `a0`。



#### 8.对照与性能

`verify/ref_model_checker` 在每条指令退休时，用功能模型再执行一步，比较 PC、寄存器写回和访存。不一致会在提交级报错并停机。

`modules/perf` 在仿真结束时做 Top-Down：把发射槽分成退休、前端停顿、后端停顿（再拆成访存与核内）和错误投机（分支预测失败、load replay）。计数来自各模块写入的 `PerfStats`。



#### 9.设计笔记

| 文档 | 内容 |
| --- | --- |
| [docs/RV-TCore.md](docs/RV-TCore.md) | 流水段划分、各单元的端口和队列规模 |
| [docs/VectorCore-Design.md](docs/VectorCore-Design.md) | 向量执行域的阶段计划 |
| [docs/TensorCore-Design.md](docs/TensorCore-Design.md) | Tile / Tensor 执行域 |
| [docs/Vector-Tile-Rename.md](docs/Vector-Tile-Rename.md) | 向量与 Tile 重命名 |
| [docs/VRegFile-TRegFile.md](docs/VRegFile-TRegFile.md) | VPRF / TPRF |
| [docs/PerformanceStatistic-Design.md](docs/PerformanceStatistic-Design.md) | 性能计数与 Top-Down |
| [docs/KanataTrace.md](docs/KanataTrace.md) | Konata 流水线 trace 的设计（尚未接入仿真） |
| [docs/Read_ELF.md](docs/Read_ELF.md) | ELF 加载说明 |
