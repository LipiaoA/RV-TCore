#ifndef RV_TCORE_CONFIG_H
#define RV_TCORE_CONFIG_H
#include "rv_def.h"
enum class InclusionPolicy { NonInclusive = 0, Inclusive = 1, Exclusive = 2 };

struct CacheConfig {
    u32 size = 32 * 1024;
    u32 line_size = 64;
    u32 ways = 4;
    u32 fetch_width = 4;
    // latencies in cycles
    u32 hit_latency = 1;
    u32 miss_penalty = 10;   // time to start lower request
    u32 install_latency = 1; // time to install after data arrives
    u32 writeback_latency = 5;
    InclusionPolicy inclusion_policy = InclusionPolicy::Inclusive;
    CacheConfig() = default;
    CacheConfig(u32 sz, u32 ls, u32 ways, u32 fw=4):
        size(sz), line_size(ls), ways(ways), fetch_width(fw) {}
};

typedef struct {
    u32 fetch_width { 4 };
} FetchConfig;

typedef struct {
    u32 width { 4 };
} RenameConfig;

struct ALUConfig {
    u32 width { 4 };
    u32 latency { 1 };
    u32 issue_width { 2 };
    u32 stages { 8 };
};

typedef struct {
    u32 width { 4 };
    u32 latency { 1 };
    u32 issue_width { 2 };
    u32 stages { 8 };
} AGUConfig;

typedef struct {
    u32 width { 4 };
    u32 latency { 1 };
    u32 issue_width { 2 };
    u32 stages { 8 };
} FPUConfig;

typedef struct {
    u32 load_q_size { 128 };
    u32 store_q_size { 128 };
    u32 agus_num { 4 };
    u32 mshr_buffer_size { 128 };
    u32 issue_width { 2 };
} LSUConfig;

typedef struct {
    u32 width { 4 };
    u32 stages { 8 };
} DecodeConfig;


typedef struct {
    u32 alu_num { 0 };
    u32 fpu_num { 0 };
    u32 bru_num { 0 };
} ExecConfig;

typedef struct {
    u32 int_issue_width { 4 };
    u32 ls_issue_width { 2 };
    u32 fp_issue_width { 2 };
} IssueConfig;

typedef struct {
    u32 width { 4 };
} CommitConfig;

typedef struct {
    u32 width { 4 };
    u32 latency { 1 };
    u32 issue_width { 2 };
    u32 stages { 8 };
} BRUConfig;

typedef struct {
    u32 capacity { 64 };
} ROBConfig;

typedef struct {
    u32 capacity { 64 };
} IQConfig;

typedef struct {
    u32 capacity { 32 };
} LQConfig;

typedef struct {
    u32 capacity { 32 };
} SQConfig;

typedef struct {
    // pipeline
    int fetch_width  = 4;
    int decode_width = 4;
    int rename_width = 4;
    int commit_width = 4;

    // queues / structures
    int rob_entries = 128;
    int iq_entries  = 64;
    int lq_entries  = 32;
    int sq_entries  = 32;

    // LSU / memory subsystem (scalar knobs used by LSUConfig)
    u32 load_q_size = 128;
    u32 store_q_size = 128;
    u32 agus_num = 4;
    u32 mshr_buffer_size = 128;
    u32 lsu_issue_width = 2;

    // issue / execute widths
    u32 int_issue_width = 4;
    u32 ls_issue_width = 2;
    u32 fp_issue_width = 2;
    u32 alu_num = 6;
    u32 fpu_num = 2;
    u32 bru_num = 1;

    // i-TLB related
    u32 itlb_hit_latency = 12;
    u32 itlb_miss_penalty = 3;
    u32 itlb_install_latency = 1;

    // d-TLB / LSU-facing dcache timing (kept in sync with dcache_* below by init)
    u32 dtlb_hit_latency = 12;
    u32 dtlb_miss_penalty = 3;
    u32 dtlb_install_latency = 1;

    // d-cache related
    u32 dcache_size = 64 * 1024;
    u32 dcache_hit_latency = 2;
    u32 dcache_miss_penalty = 8;
    u32 dcache_install_latency = 2;
    u32 dcache_inclusive = 1;       // 0: NonInclusive, 1: Inclusive, 2: Exclusive
    u32 dcache_line_size = 64;
    u32 dcache_ways = 4;
    u32 dcache_fetch_width = 4;
    u32 dcache_wb_latency = 5;

    // i-cache related
    u32 icache_size = 32 * 1024;
    u32 icache_line_size = 64;
    u32 icache_ways = 4;
    u32 icache_fetch_width = 4;
    u32 icache_hit_latency = 1;
    u32 icache_miss_penalty = 3;
    u32 icache_install_latency = 1;
    u32 icache_writeback_latency = 8;
    u32 icache_inclusive = 1;       // 0: NonInclusive, 1: Inclusive, 2: Exclusive

    // L2 cache related
    u32 l2_size = 512 * 1024;
    u32 l2_ways = 8;
    u32 l2_line_size = 64;
    u32 l2_fetch_width = 4;
    u32 l2_hit_latency = 10;
    u32 l2_miss_penalty = 30;
    u32 l2_install_latency = 5;
    u32 l2_writeback_latency = 12;
    u32 l2_inclusive = 1;       // 0: NonInclusive, 1: Inclusive, 2: Exclusive

    // legacy aliases (optional consumers)
    int l1iSize = 32 * 1024;
    int l1dSize = 32 * 1024;
} CoreConfig;


class TCoreConfig {
private:
    TCoreConfig();
    explicit TCoreConfig(const CoreConfig& core);

public:
    ~TCoreConfig();

    static TCoreConfig& get_instance();


    DecodeConfig                            decode_config() const;
    LSUConfig                               lsu_config() const;
    CacheConfig                             l2_config() const;
    CacheConfig                             icache_config() const;
    CacheConfig                             dcache_config() const;
    IssueConfig                             issue_config() const;
    ROBConfig                               rob_config() const;
    RenameConfig                            rename_config() const;
    BRUConfig                               bru_config() const;
    FetchConfig                             fetch_config() const;
    CommitConfig                            commit_config() const;
    ExecConfig                              execute_config() const;

private:
    void                                    init_config();
    void                                    print_config() const;

private:
    CoreConfig                              m_core_config;
};

#endif // RV_TCORE_CONFIG_H
