#ifndef TOPDOWN_H
#define TOPDOWN_H

#include "perf/perf_def.h"

/*
 * @brief Topdown performance analyzer
 *
 * @details
 * This class is used to analyze the performance of the topdown pipeline.
 */

class Topdown {
public:
    explicit Topdown(PerfStats &stats);
    ~Topdown();

    void                        analyze();
    void                        report();
private:
    PerfStats&                  m_stats;
};

#endif // TOPDOWN_H