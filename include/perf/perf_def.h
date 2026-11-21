#ifndef PERF_DEF_H
#define PERF_DEF_H

#include <array>

#include "util/util.h"

typedef struct CacheStats {
    u64 accesses { 0 };
    u64 hits { 0 };
    u64 misses { 0 };
    u64 total_latency { 0 };
} CacheStats;

enum class BubbleCause : u8 {
    None = 0,
    Recovery,
    NoReady,
    Lsq,
    Rob,
    FreeList,
    VecFreeList,
    TileFreeList,
    ICache,
    FetchBw,
    FuBusy,
    COUNT
};

enum class PerfEventType : u8 {
    BranchMispredict,
    FrontendStall,
    ICacheMiss,
    ITLBMiss,
    LoadReplay,
    StoreLoadViolation,
    DCacheMiss,
    TLBMiss,
    IssueQueueFull,
    ROBFull,
    Commit
};

struct PerfEvent {
    PerfEventType type;
    u64 seq;
    u32 cycles;
};

struct CycleStats {
    u64 cycle = 0;

    // 当前周期的关键状态
    u32 committed = 0;
    u32 frontend_valid = 0;
    u32 dispatched = 0;
    u32 issued = 0;

    // 当前周期的主要阻塞原因
    bool frontend_stalled = false;
    bool recovery_active = false;
    bool backend_stalled = false;

    // 其他专项统计所需的计数器
    u32 replay_count = 0;
    u32 branch_mispredict_count = 0;
};


struct PerfStats {
    constexpr static u32 MAX_FU_NUM = 8;
    constexpr static u32 MAX_FLUSH_COUNT = 32;
    struct FrontendStats {
        u64 fetch_packets { 0 };
        u64 fetch_insts { 0 };
        u64 fetch_stall_cycles { 0 };
        u64 redirect_count { 0 };
        u64 branch_mispredict_count { 0 };
        u64 icache_miss_count { 0 };
        u64 itlb_miss_count { 0 };
        u64 flushed_insts { 0 };

        u64 fetch_bubbles { 0 };
        u64 recovery_slots { 0 };

        u64 latency { 0 };
        u64 bandwidth { 0 };

        u64 fetch_id { 0 };
        bool icache_wait { false };
    };

    struct RenameStats {
        u64 input_insts { 0 };
        u64 renamed_insts { 0 };
        u64 dispatched_insts { 0 };
        u64 rob_full_stall_cycles { 0 };
        u64 free_list_stall_cycles { 0 };
        u64 lq_full_stall_cycles { 0 };
        u64 sq_full_stall_cycles { 0 };
        u64 vec_free_list_stall_cycles { 0 };
        u64 tile_free_list_stall_cycles { 0 };
        u64 flushed_insts { 0 };
    };

    struct IssueStats {
        u64 selected_insts { 0 };
        u64 issued_insts { 0 };
        u64 retired_uops { 0 };
        u64 flushed_uops { 0 };
        u64 replayed_uops { 0 };
        u64 inflight_uops { 0 };
        u64 fu_busy_count { 0 };  // 因端口繁忙而未发射指令总次数
        u64 flushed_insts { 0 };
        u64 no_ready_inst_cycles { 0 }; // 因未就绪而未发射指令总周期数
        std::array<u64, MAX_FLUSH_COUNT> flushed_uops_by_type {};
        std::array<u64, MAX_FLUSH_COUNT> recovery_slots_by_type {};
    };

    struct FuStats {
        u64 accepted_uops { 0 };
        u64 flushed_uops { 0 };
        u64 completed_uops { 0 };
        u64 rejected_uops { 0 };
        u64 succ_direction { 0 };
        u64 fail_direction { 0 };
        u64 succ_target { 0 };
        u64 fail_target { 0 };
        u64 busy_cycles { 0 };
    };

    struct CommitStats {
        constexpr static u32 MAX_INST_TYPES = 16;
        u64 resolved_insts { 0 };
        u64 retired_insts { 0 };
        u64 rob_empty_cycles { 0 };
        u64 head_not_ready_cycles { 0 };
        u64 head_not_ready_load { 0 };
        u64 head_not_ready_store { 0 };
        u64 head_not_ready_other { 0 };
        std::array<u64, MAX_INST_TYPES> retired_by_type {};
    };

    struct FlushStats {
        u64 requests { 0 };
        u64 events { 0 };
        std::array<u64, MAX_FLUSH_COUNT> by_type {};
    };

    u64 cycles { 0 };
    u64 stats_level { 1 }; // 0: not count, 1: count all, 2: 待定
    FrontendStats frontend;
    RenameStats rename;
    IssueStats issue;
    FuStats fu[MAX_FLUSH_COUNT][MAX_FU_NUM];
    CommitStats commit;
    FlushStats flush;
    std::array<u64, static_cast<size_t>(BubbleCause::COUNT)> bubble_slots {};
    BubbleCause rename_stall { BubbleCause::None };
    CacheStats icache {};
    CacheStats dcache {};
    CacheStats itlb {};
    CacheStats dtlb {};
    u64 load_replay_sb { 0 };
    u64 load_replay_alias { 0 };

    // MemStalls（按周期计数，供 Memory Bound = (AnyLoad+Stores)/Clocks）
    u64 mem_stall_any_load { 0 };
    u64 mem_stall_stores { 0 };

    inline bool perf() const
    {
        return stats_level > 0;
    }

    void reset()
    {
        const u64 level = stats_level;
        *this = PerfStats {};
        stats_level = level;
    }

    double ipc() const
    {
        return cycles == 0 ? 0.0 : static_cast<double>(commit.retired_insts) / static_cast<double>(cycles);
    }
};


#endif // PERF_DEF_H
