## 性能统计实现方案

### 总体架构

```text
                    CPU Core
    Fetch
    Decode
    Rename
    Dispatch
    Issue
    ROB
    LSU
    BP
    ICache
    DCache
    TLB
       |
       v
+-----------------------+
|   PerfManager         |
+-----------------------+
     |         |
     |         |
     v         v
 Snapshot   Event

     |         |
     |         |
+----+----+----+----------------+
|         |                     |
v         v                     v

TopDown  Profile          Trace(optional)
          |
          +-- BranchProfile
          +-- CacheProfile
          +-- TLBProfile
          +-- FUProfile
```

------

### 第一层：Snapshot（状态统计）

这是 TopDown 的核心数据来源。

#### CpuSnapshot

```cpp
struct CpuSnapshot
{
    uint32_t cycle;

    uint32_t fetch_slots;
    uint32_t decode_slots;
    uint32_t rename_slots;
    uint32_t issue_slots;
    uint32_t commit_slots;

    bool fetch_stall;
    bool decode_stall;
    bool rename_stall;

    bool rob_full;
    bool iq_full;
    bool lq_full;
    bool sq_full;

    bool branch_redirect;
    bool replay;

    uint32_t rob_occ;
    uint32_t iq_occ;

    uint32_t issued;
    uint32_t committed;
};
```

每 Cycle：

```cpp
CpuSnapshot snap;
collectSnapshot(snap);
perf.sample(snap);
```

------

### 第二层：Event（事件统计）

用于统计：

```text
分支预测命中率
Cache命中率
TLB命中率
Replay次数
Flush次数
```

定义：

```cpp
enum class PerfEvent {
    BranchHit,
    BranchMiss,
    ICacheHit,
    ICacheMiss,
    DCacheHit,
    DCacheMiss,
    ITLBHit,
    ITLBMiss,
    DTLBHit,
    DTLBMiss,
    LSUReplay,
    Flush,
};
```

模块内部：

```cpp
perf.event(PerfEvent::BranchMiss);
```

------

#### TopDown Analyzer

```cpp
class TopDownAnalyzer
{
public:
    void sample(const CpuSnapshot& s);
};
```

统计：

```text
Retiring
Frontend Bound
Backend Bound
Bad Speculation
```

------

#### TopDown 判断方式

```cpp
if (s.committed) {
    retiring += s.committed;
} else if (s.branch_redirect) {
    badspec += issue_width;
} else if (s.fetch_stall) {
    frontend += issue_width;
} else {
    backend += issue_width;
}
```

------

#### 二级 TopDown

Frontend：

```text
ICache Miss
ITLB Miss
BTB Miss
Fetch Bandwidth
```

Backend：

```text
ROB Full
IQ Full
LQ Full
SQ Full
Core Bound
Memory Bound
```

BadSpec：

```text
Branch Mispredict
Machine Clear
Replay
```

Retiring：

```text
Load
Store
ALU
Branch
Vector
Tensor
```

------

### 第三层：Profile

#### BranchProfile

```cpp
class BranchProfile
{
    uint64_t hit;
    uint64_t miss;
};
```

输出：

```text
BP Accuracy : 97.6%

Hit  : 12,345,678
Miss :    300,000
```

------

#### CacheProfile

```cpp
class CacheProfile
{
    uint64_t hit;
    uint64_t miss;
};
```

输出：

```text
L1D
Hit Rate : 95.3%
Hit  : xxx
Miss : xxx
```

------

#### TLBProfile

```cpp
ITLB
DTLB
```

分别统计。

------

#### FUProfile

这个非常有价值。

统计：

```cpp
ALU busy cycles
BRU busy cycles
AGU busy cycles
MUL busy cycles
DIV busy cycles
```

每拍：

```cpp
alu_busy += alu.numBusyPipe();
```

最终：

```text
ALU Utilization
62.3%
```

------

#### Provider机制

定义：

```cpp
class PerfProvider
{
public:
    virtual void fillSnapshot(CpuSnapshot&) const {}
};
```

例如：

```cpp
class ROB : public PerfProvider
{
    void fillSnapshot(CpuSnapshot& s) const override
    {
        s.rob_full = full();
        s.rob_occ  = occupancy();
    }
};
```

CPU初始化：

```cpp
perf.registerProvider(&rob);
perf.registerProvider(&iq);
perf.registerProvider(&lsu);
perf.registerProvider(&fetch);
```

每Cycle：

```cpp
CpuSnapshot snap;

for(auto* p : providers)
{
    p->fillSnapshot(snap);
}

topdown.sample(snap);
```

这样新增：

```text
Vector Cluster
Tensor Cluster
L2 Cache
Prefetcher
```

无需修改 PerfManager。

------

### 目录结构

```text
perf/
├── PerfManager.h
├── snapshot/
│   ├── CpuSnapshot.h
│   └── SnapshotCollector.h
├── event/
│   ├── PerfEvent.h
│   └── EventCounter.h
├── topdown/
│   ├── TopDownAnalyzer.h
│   ├── TopDownLevel1.h
│   └── TopDownLevel2.h
├── profile/
│   ├── BranchProfile.h
│   ├── CacheProfile.h
│   ├── TLBProfile.h
│   └── FUProfile.h
└── trace/
    └── TraceRecorder.h
```

对于处理器性能分析而言，最稳妥的原则是：

```text
TopDown      → Snapshot（状态）
Cache/TLB/BP → Event（事件）
Utilization  → Snapshot（状态）
命中率统计    → Event（事件）
调试Trace     → Snapshot
```

这样既不会把统计代码侵入到所有模块，也能支持你后续扩展到 BOOM 级别的 Top-Down、Cache、TLB、分支预测和执行单元利用率分析。