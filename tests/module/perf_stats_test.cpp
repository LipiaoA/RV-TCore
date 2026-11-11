#include <cassert>
#include <cmath>
#include <cstdlib>
#include <string>

#include "module_comm.h"
#include "perf/perf.h"

int main()
{
    PerfStats stats;

    stats.cycles = 10;
    stats.commit.retired_insts = 15;
    stats.flush.events = 2;
    stats.flush.by_type[static_cast<size_t>(FlushType::BRANCH_MISPRED)] = 2;

    assert(stats.cycles == 10);
    assert(stats.commit.retired_insts == 15);
    assert(std::fabs(stats.ipc() - 1.5) < 1e-9);
    assert(stats.flush.events == 2);
    assert(stats.flush.by_type[static_cast<size_t>(FlushType::BRANCH_MISPRED)] == 2);
    assert(std::string(FlushTypeStr[static_cast<size_t>(FlushType::BRANCH_MISPRED)])
           == "BRANCH_MISPRED");

    stats.reset();
    assert(stats.cycles == 0);
    assert(stats.commit.retired_insts == 0);
    assert(stats.flush.events == 0);
    assert(stats.flush.by_type[static_cast<size_t>(FlushType::BRANCH_MISPRED)] == 0);
    assert(stats.ipc() == 0.0);

    return EXIT_SUCCESS;
}
