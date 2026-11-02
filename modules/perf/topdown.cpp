#include "topdown.h"

Topdown::Topdown(PerfStats &stats)
    : m_stats(stats)
{
}

Topdown::~Topdown()
{
}

void Topdown::analyze()
{
    return;
}

void Topdown::report()
{
    using namespace std;
    cout << setfill('.');
    cout << "PerfStats\n";
    cout << "  Cycles: " << m_stats.cycles << endl;
    cout << "  Frontend: " << endl;
    cout << "    Fetch Packets: " << m_stats.frontend.fetch_packets << endl;
    cout << "    Fetch Insts: " << m_stats.frontend.fetch_insts << endl;
    cout << "    Fetch Stall Cycles: " << m_stats.frontend.fetch_stall_cycles << endl;
    cout << "    Redirect Count: " << m_stats.frontend.redirect_count << endl;
    cout << "    Branch Mispredict Count: " << m_stats.frontend.branch_mispredict_count << endl;
    cout << "    ICache Miss Count: " << m_stats.frontend.icache_miss_count << endl;
    cout << "    ITLB Miss Count: " << m_stats.frontend.itlb_miss_count << endl;
    cout << "    Flushed Insts: " << m_stats.frontend.flushed_insts << endl;
    cout << "  Rename: " << endl;
    cout << "    Input Insts: " << m_stats.rename.input_insts << endl;
    cout << "    Renamed Insts: " << m_stats.rename.renamed_insts << endl;
    cout << "    Dispatched Insts: " << m_stats.rename.dispatched_insts << endl;
    cout << "    ROB Full Stall Cycles: " << m_stats.rename.rob_full_stall_cycles << endl;
    cout << "    Free List Stall Cycles: " << m_stats.rename.free_list_stall_cycles << endl;
    cout << "    Flushed Insts: " << m_stats.rename.flushed_insts << endl;
    cout << "  Issue: " << endl;
    cout << "    Selected Insts: " << m_stats.issue.selected_insts << endl;
    cout << "    Issued Insts: " << m_stats.issue.issued_insts << endl;
    cout << "    Fu Busy Count: " << m_stats.issue.fu_busy_count << endl;
    cout << "    Flushed Insts: " << m_stats.issue.flushed_insts << endl;
    cout << "    No Ready Inst Cycles: " << m_stats.issue.no_ready_inst_cycles << endl;
    cout << "  Fu: " << endl;
    cout << "   Typ | id | Accepted Uops | Flushed Uops | Completed Uops | Rejected Uops |" << endl;

    cout << "  Commit: " << endl;
    cout << "    Resolved Insts: " << m_stats.commit.resolved_insts << endl;
    cout << "    Retired Insts: " << m_stats.commit.retired_insts << endl;
    cout << "    ROB Empty Cycles: " << m_stats.commit.rob_empty_cycles << endl;
    cout << "    Head Not Ready Cycles: " << m_stats.commit.head_not_ready_cycles << endl;
    cout << "  Flush: " << endl;
    cout << "    Requests: " << m_stats.flush.requests << endl;
    cout << "    Events: " << m_stats.flush.events << endl;
    cout << "    By Type: " << endl;
    return;
}
