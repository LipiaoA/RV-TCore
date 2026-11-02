#ifndef PERF_COUNTER_H
#define PERF_COUNTER_H

#include "perf_def.h"

/*
 * @brief Performance counter
 *
 * @details
 * replay branch cache tlb  etc.
 */

class PerfCounter {
public:
    PerfCounter();
    ~PerfCounter();
};

#endif // PERF_COUNTER_H