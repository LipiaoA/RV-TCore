#ifndef TOPDOWN_H
#define TOPDOWN_H

#include "perf/perf_def.h"

/*
 * @brief Topdown performance analyzer
 *
 * @details
 * This class is used to analyze the performance of the topdown pipeline.
*/

struct TopDownCounters {
    u64 cycles = 0;

    u64 retired_uops = 0;
    u64 issued_uops = 0;

    u64 recovery_bubbles = 0;
    u64 frontend_bubble_slots = 0;

    u64 branch_mispredict_slots = 0;
    u64 replay_slots = 0;

    u64 memory_bound_slots = 0;
    u64 core_bound_slots = 0;

    u64 rob_full_cycles = 0;
    u64 iq_full_cycles = 0;
    u64 lq_full_cycles = 0;
    u64 sq_full_cycles = 0;
};

typedef struct TopdownRes {
    u64 total_cycles { 0 };
    u64 total_slots { 0 };
    u64 total_insts { 0 };


    f64 ipc {0.0};
    // Top Level
    f64 bad_speculation { 0.0 };
    f64 retiring { 0.0 };
    f64 frontend_bound { 0.0 };
    f64 backend_bound { 0.0 };

    // Bad Speculation
    u64 branch_mispredict_slots;
    u64 load_replay_slots;
    u64 memory_order_violation_slots;
    u64 other_flush_slots;

    f64 branch_mispredict { 0.0 };
    f64 load_replay { 0.0 };

    // Frontend Bound Level
    f64 frontend_latency = 0.0;
    f64 frontend_bandwidth = 0.0;

    f64 memory_bound = 0.0;
    f64 core_bound = 0.0;
} TopdownRes;

class Topdown {
public:
    explicit Topdown(PerfStats &stats);
    ~Topdown();

    void                        analyze();
    void                        report();
private:
    PerfStats&                  m_stats;
    TopdownRes                  m_res;
};

#endif // TOPDOWN_H
