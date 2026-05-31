#include <cassert>
#include <cstdlib>
#include <string>

#include "perf/perf_stats.h"

int main()
{
    PerfStats stats;

    stats.cycles = 10;
    stats.commit.retired_insts = 15;
    stats.flush.events = 2;
    stats.flush.by_type[static_cast<size_t>(FlushType::BRANCH_MISPRED)] = 2;

    std::string dump = stats.dump();
    assert(dump.find("cycles: 10") != std::string::npos);
    assert(dump.find("retired_insts: 15") != std::string::npos);
    assert(dump.find("ipc: 1.50") != std::string::npos);
    assert(dump.find("BRANCH_MISPRED: 2") != std::string::npos);

    stats.reset();
    assert(stats.cycles == 0);
    assert(stats.commit.retired_insts == 0);
    assert(stats.flush.events == 0);
    assert(stats.flush.by_type[static_cast<size_t>(FlushType::BRANCH_MISPRED)] == 0);

    return EXIT_SUCCESS;
}
