#ifndef RV_TCORE_CONFIG_H
#define RV_TCORE_CONFIG_H
#include "rv_def.h"

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
    u32 dcache_hit_latency { 2 };
    u32 dcache_miss_penalty { 8 };
    u32 dcache_install_latency { 2 };
    u32 tlb_hit_latency { 12 };
    u32 tlb_miss_penalty { 3 };
    u32 tlb_install_latency { 1 };
} LSUConfig;

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
    int fetchWidth  = 4;
    int decodeWidth = 4;
    int renameWidth = 4;
    int commitWidth = 4;

    // queues / structures
    int robEntries = 128;
    int iqEntries  = 64;
    int lqEntries  = 32;
    int sqEntries  = 32;

    // cache
    int l1iSize = 32 * 1024;
    int l1dSize = 32 * 1024;
} CoreConfig;


class TCoreConfig {
private:
    TCoreConfig() = default;

public:
    ~TCoreConfig() = default;

public:
    static TCoreConfig& get_instance() {
        static TCoreConfig instance;
        instance.init_config();
        return instance;
    }

private:
    void init_config() {}
    void print_config() {}

};

#endif
