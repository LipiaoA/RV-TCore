#ifndef RV_TCORE_PERF_STATS_H
#define RV_TCORE_PERF_STATS_H

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include "module_comm.h"

struct PerfStats {
    struct FrontendStats {
        u64 fetch_packets { 0 };
        u64 fetch_insts { 0 };
        u64 icache_wait_cycles { 0 };
        u64 decode_queue_full_cycles { 0 };
        u64 flushed_insts { 0 };
    };

    struct RenameStats {
        u64 input_insts { 0 };
        u64 renamed_insts { 0 };
        u64 dispatched_insts { 0 };
        u64 rob_full_stall_cycles { 0 };
        u64 free_list_stall_cycles { 0 };
        u64 flushed_insts { 0 };
    };

    struct IssueStats {
        u64 enqueued_insts { 0 };
        u64 selected_insts { 0 };
        u64 issued_insts { 0 };
        u64 ready_stall_cycles { 0 };
        u64 fu_busy_stall_cycles { 0 };
        u64 flushed_insts { 0 };
    };

    struct ExecuteStats {
        u64 dispatch_accepted { 0 };
        u64 dispatch_rejected { 0 };
        u64 completed_insts { 0 };
        u64 writeback_insts { 0 };
        u64 flushed_insts { 0 };
    };

    struct CommitStats {
        u64 resolved_insts { 0 };
        u64 retired_insts { 0 };
        u64 empty_cycles { 0 };
        u64 head_not_ready_cycles { 0 };
    };

    struct FlushStats {
        u64 requests { 0 };
        u64 events { 0 };
        std::array<u64, static_cast<size_t>(FlushType::FLUSH_TYPE_COUNT)> by_type {};
    };

    u64 cycles { 0 };
    FrontendStats frontend;
    RenameStats rename;
    IssueStats issue;
    ExecuteStats execute;
    CommitStats commit;
    FlushStats flush;

    void reset()
    {
        *this = PerfStats {};
    }

    double ipc() const
    {
        return cycles == 0 ? 0.0 : static_cast<double>(commit.retired_insts) / static_cast<double>(cycles);
    }

    std::string dump() const
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "PerfStats\n";
        os << "cycles: " << cycles << "\n";
        os << "retired_insts: " << commit.retired_insts << "\n";
        os << "ipc: " << ipc() << "\n";
        os << "frontend.fetch_packets: " << frontend.fetch_packets << "\n";
        os << "frontend.fetch_insts: " << frontend.fetch_insts << "\n";
        os << "rename.renamed_insts: " << rename.renamed_insts << "\n";
        os << "issue.issued_insts: " << issue.issued_insts << "\n";
        os << "execute.completed_insts: " << execute.completed_insts << "\n";
        os << "commit.resolved_insts: " << commit.resolved_insts << "\n";
        os << "flush.requests: " << flush.requests << "\n";
        os << "flush.events: " << flush.events << "\n";
        for (size_t i = 0; i < flush.by_type.size(); ++i) {
            if (flush.by_type[i] == 0) {
                continue;
            }
            os << FlushTypeStr[i] << ": " << flush.by_type[i] << "\n";
        }
        return os.str();
    }
};

#endif
