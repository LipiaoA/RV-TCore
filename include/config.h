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
    u32 miss_penalty = 3;   // time to start lower request 10
    u32 install_latency = 1; // time to install after data arrives
    u32 writeback_latency = 5;
    InclusionPolicy inclusion_policy = InclusionPolicy::Inclusive;
    CacheConfig() = default;
    CacheConfig(u32 sz, u32 ls, u32 ways, u32 fw=4):
        size(sz), line_size(ls), ways(ways), fetch_width(fw) {}
};

struct TageConfig
{
    u32 ntables = 5;

    u32 table_size[5]  = {1024,1024,1024,1024,1024};
    u32 hist_len[5] = {4,8,16,32,64};
    u32 tag_bits[5]    = {8,8,9,9,10};

    u32 base_size = 16384;   // BIM size
};

struct BIMConfig {
    u32 entries = 0x4000;
    u32 hist_len = 16;
    u32 fetch_width = 4;
};

struct BTBConfig {
    u32 nsets = 256;
    u32 ways = 4;
    u32 tag_bits = 16;
    u32 pc_lowdrop = 2;
};

struct BPConfig {
    TageConfig tage_config;
    BIMConfig bim_config;
    BTBConfig btb_config;
    u32 ras_capacity = 64;
};


typedef struct {
    u32 capacity { 64 };
    u32 tag_bits { 10 };
} RASConfig;

typedef struct {
    u32 fetch_width { 4 };
    u32 group_size { 4 };
    u32 bank_size { 4 };
} FetchConfig;

typedef struct {
    u32 width { 4 };
    u32 prf_capacity { 192 };
} RenameConfig;

typedef struct {
    u32 width { 4 };
    u32 capacity { 192 };
} RegFileConfig;

struct ALUConfig {
    u32 stages {3};
};

typedef struct {
    u32 width { 4 };
    u32 latency { 1 };
    u32 issue_width { 2 };
    u32 stages { 8 };
} FPUConfig;

typedef struct {
    u32 load_q_size { 128 };
    u32 store_q_size { 128 };
    u32 agu_num { 4 };
    u32 mshr_buffer_size { 128 };
    u32 issue_width { 2 };
    u32 agu_stages { 4 };
} LSUConfig;

typedef struct {
    u32 width { 4 };
    u32 stages { 8 };
} DecodeConfig;

typedef struct {
    u32 width { 4 };
    u32 stages { 4 };
} BRUConfig;

typedef struct {
    u32 alu_num { 0 };
    u32 fpu_num { 0 };
    u32 bru_num { 0 };
    u32 agu_num { 0 };
    u32 commit_width { 4 };
    ALUConfig alu_conf;
    FPUConfig fpu_conf;
    BRUConfig bru_conf;
    LSUConfig lsu_conf;
} ExecConfig;

typedef struct {
    /* reserved interface for future use */
    u32 width { 4 };
} FlushUnitConfig;

typedef struct {
    u32 int_issue_width { 4 };
    u32 ls_issue_width { 2 };
    u32 fp_issue_width { 2 };
} IssueConfig;

typedef struct {
    u32 width { 4 };
    u32 resolve_width { 4 };
    u32 commit_width {4};
} CommitConfig;

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
    u32 fetch_width  = 4;
    u32 fetch_group_size = 32; // 32 bytes
    u32 fetch_bank_size = 16;  // 16 bytes
    u32 decode_width = 4;
    u32 rename_width = 4;
    u32 commit_width = 4;
    u32 exec_commit_width = 4;

    u32 prf_width = 4;
    u32 prf_capacity = 192;           // physical register file size

    // queues / structures
    u32 rob_entries = 128;
    u32 iq_entries  = 64;
    u32 lq_entries  = 32;
    u32 sq_entries  = 32;

    // LSU / memory subsystem (scalar knobs used by LSUConfig)
    u32 load_q_size = 128;
    u32 store_q_size = 128;
    u32 agu_num = 4;
    u32 mshr_buffer_size = 128;
    u32 lsu_issue_width = 2;
    u32 alu_stages = 3;
    u32 fpu_stages = 4;
    u32 bru_stages = 3;

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
    u32 l2_hit_latency = 5;
    u32 l2_miss_penalty = 8;
    u32 l2_install_latency = 5;
    u32 l2_writeback_latency = 8;
    u32 l2_inclusive = 1;       // 0: NonInclusive, 1: Inclusive, 2: Exclusive

    // legacy aliases (optional consumers)
    int l1iSize = 32 * 1024;
    int l1dSize = 32 * 1024;

    // branch predictor related
    u32 bim_entries = 0x4000;
    u32 bim_hist_len = 16;
    u32 bim_fetch_width = 4;
    u32 btb_nsets = 256;
    u32 btb_ways = 4;
    u32 btb_tag_bits = 16;
    u32 btb_pc_lowdrop = 2;

    u32 ras_capacity = 64;
    u32 ras_tag_bits = 10;

    // flush unit related
    u32 flush_width = 4;

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
    RASConfig                               ras_config() const;
    TageConfig                              tage_config() const;
    BIMConfig                               bim_config() const;
    BTBConfig                               btb_config() const;
    BPConfig                                bp_config() const;
    RegFileConfig                           regfile_config() const;
    FlushUnitConfig                         flush_config() const;
private:
    void                                    init_config();
    void                                    print_config() const;

private:
    CoreConfig                              m_core_config;
};

#endif // RV_TCORE_CONFIG_H
