#include "topdown.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "module_comm.h"

namespace {

constexpr int kFieldWidth = 85;
constexpr int kValueWidth = 16;

const char* fu_type_name(u32 t)
{
    static constexpr const char* kNames[] = {
        "NONE", "ALU", "BRU", "LSU", "AGU", "STD", "FPU", "VPU", "TPU"
    };
    if (t >= (sizeof(kNames) / sizeof(kNames[0]))) {
        return "?";
    }
    return kNames[t];
}

std::string fmt_u64(u64 value)
{
    return std::to_string(value);
}

std::string fmt_f64(double value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

void print_kv(const std::string& title, const std::string& value, int indent = 0)
{
    const std::string labeled = std::string(static_cast<size_t>(indent), ' ') + title;
    int dots = kFieldWidth - static_cast<int>(labeled.size()) - 1;
    if (dots < 2) {
        dots = 2;
    }
    std::cout << labeled << ' '
              << std::string(static_cast<size_t>(dots), '.')
              << ' '
              << std::left << std::setw(kValueWidth) << value
              << '\n';
}

void print_kv(const std::string& title, u64 value, int indent = 0)
{
    print_kv(title, fmt_u64(value), indent);
}

void print_kv(const std::string& title, f64 value, int indent = 0)
{
    print_kv(title, fmt_f64(value), indent);
}

void print_pct(const std::string& title, f64 ratio, int indent = 0)
{
    print_kv(title, fmt_f64(ratio * 100.0) + "%", indent);
}

void print_section(const char* name)
{
    std::cout << '\n' << name << '\n';
}

void print_table_border(const int* widths, size_t n)
{
    std::cout << "  +";
    for (size_t i = 0; i < n; ++i) {
        std::cout << std::string(static_cast<size_t>(widths[i] + 2), '-') << '+';
    }
    std::cout << '\n';
}

void print_table_row(const int* widths, const std::string* cols, size_t n)
{
    std::cout << "  |";
    for (size_t i = 0; i < n; ++i) {
        std::cout << ' ' << std::left << std::setw(widths[i]) << cols[i] << " |";
    }
    std::cout << '\n';
}

bool fu_idle(const PerfStats::FuStats& s)
{
    return s.accepted_uops == 0 && s.flushed_uops == 0
        && s.completed_uops == 0 && s.rejected_uops == 0
        && s.busy_cycles == 0;
}

std::string fmt_util(u64 busy, u64 cycles)
{
    const double ratio = cycles == 0 ? 0.0 : static_cast<double>(busy) / static_cast<double>(cycles);
    return fmt_f64(ratio * 100.0) + "%";
}

const char* bubble_cause_name(BubbleCause cause)
{
    switch (cause) {
    case BubbleCause::None: return "None";
    case BubbleCause::Recovery: return "Recovery";
    case BubbleCause::NoReady: return "NoReady";
    case BubbleCause::Lsq: return "Lsq";
    case BubbleCause::Rob: return "Rob";
    case BubbleCause::FreeList: return "FreeList";
    case BubbleCause::VecFreeList: return "VecFreeList";
    case BubbleCause::TileFreeList: return "TileFreeList";
    case BubbleCause::ICache: return "ICache";
    case BubbleCause::FetchBw: return "FetchBw";
    case BubbleCause::FuBusy: return "FuBusy";
    case BubbleCause::COUNT: break;
    }
    return "?";
}

void print_fu_table(const PerfStats& stats)
{
    const int widths[] = {4, 2, 12, 12, 14, 12, 11, 8};
    const std::string header[] = {
        "Type", "Id", "Accepted Uops", "Flushed Uops", "Completed Uops", "Rejected Uops",
        "Busy Cycles", "Util"
    };
    constexpr size_t n = sizeof(widths) / sizeof(widths[0]);

    print_table_border(widths, n);
    print_table_row(widths, header, n);
    print_table_border(widths, n);

    bool any = false;
    const u32 fu_types = static_cast<u32>(FuType::NUM_FU_TYPES);
    for (u32 t = 0; t < fu_types && t < PerfStats::MAX_FLUSH_COUNT; ++t) {
        for (u32 id = 0; id < PerfStats::MAX_FU_NUM; ++id) {
            const auto& fu = stats.fu[t][id];
            if (fu_idle(fu)) {
                continue;
            }
            any = true;
            const std::string row[] = {
                fu_type_name(t), fmt_u64(id), fmt_u64(fu.accepted_uops),
                fmt_u64(fu.flushed_uops), fmt_u64(fu.completed_uops),
                fmt_u64(fu.rejected_uops), fmt_u64(fu.busy_cycles),
                fmt_util(fu.busy_cycles, stats.cycles)
            };
            print_table_row(widths, row, n);
        }
    }
    print_table_border(widths, n);
    for (u32 t = 0; t < fu_types && t < PerfStats::MAX_FLUSH_COUNT; ++t) {
        u64 accepted_uops = 0;
        u64 flushed_uops = 0;
        u64 completed_uops = 0;
        u64 rejected_uops = 0;
        u64 busy_cycles = 0;
        for (u32 id = 0; id < PerfStats::MAX_FU_NUM; ++id) {
            const auto& fu = stats.fu[t][id];
            accepted_uops += fu.accepted_uops;
            flushed_uops += fu.flushed_uops;
            completed_uops += fu.completed_uops;
            rejected_uops += fu.rejected_uops;
            busy_cycles += fu.busy_cycles;
        }
        const std::string row[] = {
            fu_type_name(t), "T", fmt_u64(accepted_uops),
            fmt_u64(flushed_uops), fmt_u64(completed_uops), fmt_u64(rejected_uops),
            fmt_u64(busy_cycles), fmt_util(busy_cycles, stats.cycles),
        };
        print_table_row(widths, row, n);
    }
    if (!any) {
        const std::string row[] = {"-", "-", "0", "0", "0", "0", "0", "0.00%"};
        print_table_row(widths, row, n);
    }
    print_table_border(widths, n);
}

void print_branch_predictor_info(const PerfStats& stats) {
    u64 direct_correct = 0;
    u64 target_correct = 0;
    u64 direct_miss = 0;
    u64 target_miss = 0;
    const u32 fu_types = static_cast<u32>(FuType::BRU);
    for (u32 id = 0; id < PerfStats::MAX_FU_NUM; ++id) {
        const auto& fu = stats.fu[fu_types][id];
        direct_correct += fu.succ_direction;
        target_correct += fu.succ_target;
        direct_miss += fu.fail_direction;
        target_miss += fu.fail_target;
    }
    f64 total_ratio = (direct_correct + target_correct) * 1.0 /
                      (direct_correct + direct_miss + target_correct + target_miss);
    print_kv("Direct Correct", direct_correct, 2);
    print_kv("Target Correct", target_correct, 2);
    print_kv("Direct Miss", direct_miss, 2);
    print_kv("Target Miss", target_miss, 2);
    print_pct("Direct Correct Ratio", direct_correct * 1.0 / (direct_correct + direct_miss), 2);
    print_pct("Target Correct Ratio", target_correct * 1.0 / (target_correct + target_miss), 2);
    print_pct("Total Correct Ratio", total_ratio, 2);
    return;
}

void print_flush_type_table(const PerfStats& stats)
{
    const int widths[] = {16, 12};
    const std::string header[] = {"Type", "Count"};
    constexpr size_t n = sizeof(widths) / sizeof(widths[0]);

    print_table_border(widths, n);
    print_table_row(widths, header, n);
    print_table_border(widths, n);

    const u32 type_count = static_cast<u32>(FlushType::FLUSH_TYPE_COUNT);
    for (u32 t = 0; t < type_count && t < PerfStats::MAX_FLUSH_COUNT; ++t) {
        const char* name = (t < (sizeof(FlushTypeStr) / sizeof(FlushTypeStr[0])))
            ? FlushTypeStr[t]
            : "?";
        const std::string row[] = {name, fmt_u64(stats.flush.by_type[t])};
        print_table_row(widths, row, n);
    }
    print_table_border(widths, n);
}

} // namespace

Topdown::Topdown(PerfStats &stats, const TCoreConfig& config)
    : m_stats(stats),
      m_res{},
      m_core_config(config)
{
}

Topdown::~Topdown()
{
}

void Topdown::analyze()
{
    const u32 issue_width = m_core_config.issue_config().int_issue_width +
                            m_core_config.issue_config().ls_issue_width;

    m_res = TopdownRes {};
    m_res.issue_width = issue_width;
    m_res.total_cycles = m_stats.cycles;
    m_res.total_slots = m_res.total_cycles * issue_width;
    m_res.total_insts = m_stats.commit.retired_insts;
    m_res.ipc = m_stats.ipc();
    if (m_res.total_slots == 0) {
        return;
    }

    const auto slots = [this](BubbleCause cause) -> u64 {
        return m_stats.bubble_slots[static_cast<size_t>(cause)];
    };
    const auto ratio = [this](u64 count) -> f64 {
        return static_cast<f64>(count) / static_cast<f64>(m_res.total_slots);
    };

    const u64 accounted_uops = m_stats.issue.retired_uops
        + m_stats.issue.flushed_uops + m_stats.issue.replayed_uops;
    m_stats.issue.inflight_uops = m_stats.issue.issued_insts >= accounted_uops
        ? m_stats.issue.issued_insts - accounted_uops
        : 0;
    m_res.inflight_uops = m_stats.issue.inflight_uops;

    const u64 retiring_slots = m_stats.issue.retired_uops;

    const u64 bad_slots = m_stats.issue.issued_insts - m_stats.issue.retired_uops
        +m_stats.frontend.recovery_slots; // FIXME: replayed uops should be accounted here ?

    const u64 frontend_bubbles = m_stats.frontend.fetch_bubbles - m_stats.frontend.recovery_slots;

    // const u64 bad_slots = m_stats.issue.flushed_uops
        // + m_stats.issue.replayed_uops + slots(BubbleCause::Recovery);

    // const u64 frontend_bubbles = slots(BubbleCause::ICache) + slots(BubbleCause::FetchBw);
    const u64 bandwidth_slots = slots(BubbleCause::FetchBw);

    // const u64 core_slots = slots(BubbleCause::NoReady) + slots(BubbleCause::Rob)
    //     + slots(BubbleCause::FreeList) + slots(BubbleCause::VecFreeList)
    //     + slots(BubbleCause::TileFreeList) + slots(BubbleCause::FuBusy);

    // // Level 2 Analysis
    // m_res.frontend_latency = ratio(m_stats.frontend.latency * fetch_width);
    // m_res.frontend_bandwidth = ratio(bandwidth_slots);


    // Top Level Analysis
    m_res.retiring = ratio(retiring_slots);
    m_res.bad_speculation = ratio(bad_slots);
    m_res.frontend_bound = ratio(frontend_bubbles);
    m_res.backend_bound = 1.0 - m_res.frontend_bound - m_res.bad_speculation - m_res.retiring;
    if (m_res.backend_bound < 0.0) {
        m_res.backend_bound = 0.0;
    }

    m_res.unaccounted = 1.0 - m_res.retiring - m_res.bad_speculation
        - m_res.frontend_bound - m_res.backend_bound;

    // Level 2：Memory Bound = (MemStalls.AnyLoad + MemStalls.Stores) / Clocks
    // Core Bound = Backend Bound - Memory Bound（Memory 不超过 Backend）
    m_res.mem_stall_any_load = m_stats.mem_stall_any_load;
    m_res.mem_stall_stores = m_stats.mem_stall_stores;
    const f64 mem_frac = static_cast<f64>(m_stats.mem_stall_any_load + m_stats.mem_stall_stores)
        / static_cast<f64>(m_res.total_cycles);
    m_res.memory_bound = std::min(mem_frac, m_res.backend_bound);
    m_res.core_bound = m_res.backend_bound - m_res.memory_bound;

    m_res.frontend_latency = ratio(slots(BubbleCause::ICache));
    m_res.frontend_bandwidth = ratio(bandwidth_slots);

    const size_t branch_type = static_cast<size_t>(FlushType::BRANCH_MISPRED);
    const size_t replay_type = static_cast<size_t>(FlushType::LOAD_REPLAY);
    const size_t order_type = static_cast<size_t>(FlushType::MEMORY_ORDER);
    m_res.branch_mispredict_slots =
        m_stats.issue.flushed_uops_by_type[branch_type]
        + m_stats.issue.recovery_slots_by_type[branch_type];
    m_res.load_replay_slots =
        m_stats.issue.flushed_uops_by_type[replay_type]
        + m_stats.issue.recovery_slots_by_type[replay_type]
        + m_stats.issue.replayed_uops;
    m_res.memory_order_violation_slots =
        m_stats.issue.flushed_uops_by_type[order_type]
        + m_stats.issue.recovery_slots_by_type[order_type];
    const u64 attributed_bad = m_res.branch_mispredict_slots
        + m_res.load_replay_slots + m_res.memory_order_violation_slots;
    m_res.other_flush_slots = bad_slots >= attributed_bad
        ? bad_slots - attributed_bad
        : 0;
    m_res.branch_mispredict = ratio(m_res.branch_mispredict_slots);
    m_res.load_replay = ratio(m_res.load_replay_slots);
    return;
}

void Topdown::print_stats()
{
    std::cout << "PerfStats\n";
    print_kv("Cycles", m_stats.cycles);
    print_kv("IPC", fmt_f64(m_stats.ipc()));
    print_kv("Stats Level", m_stats.stats_level);

    print_section("Frontend");
    print_kv("Fetch Packets", m_stats.frontend.fetch_packets, 2);
    print_kv("Fetch Insts", m_stats.frontend.fetch_insts, 2);
    print_kv("Fetch Stall Cycles", m_stats.frontend.fetch_stall_cycles, 2);
    print_kv("Redirect Count", m_stats.frontend.redirect_count, 2);
    print_kv("Branch Mispredict Count", m_stats.frontend.branch_mispredict_count, 2);
    print_kv("ICache Miss Count", m_stats.frontend.icache_miss_count, 2);
    print_kv("ITLB Miss Count", m_stats.frontend.itlb_miss_count, 2);
    print_kv("Flushed Insts", m_stats.frontend.flushed_insts, 2);
    print_kv("Fetch Bubbles", m_stats.frontend.fetch_bubbles, 2);
    print_kv("Recovery Slots", m_stats.frontend.recovery_slots, 2);

    print_section("Rename Unit");
    print_kv("Input Insts", m_stats.rename.input_insts, 2);
    print_kv("Renamed Insts", m_stats.rename.renamed_insts, 2);
    print_kv("Dispatched Insts", m_stats.rename.dispatched_insts, 2);
    print_kv("ROB Full Stall Cycles", m_stats.rename.rob_full_stall_cycles, 2);
    print_kv("Free List Stall Cycles", m_stats.rename.free_list_stall_cycles, 2);
    print_kv("LQ Full Stall Cycles", m_stats.rename.lq_full_stall_cycles, 2);
    print_kv("SQ Full Stall Cycles", m_stats.rename.sq_full_stall_cycles, 2);
    print_kv("Vec Free List Stall Cycles", m_stats.rename.vec_free_list_stall_cycles, 2);
    print_kv("Tile Free List Stall Cycles", m_stats.rename.tile_free_list_stall_cycles, 2);
    print_kv("Flushed Insts", m_stats.rename.flushed_insts, 2);

    print_section("Issue Unit");
    print_kv("Selected Insts", m_stats.issue.selected_insts, 2);
    print_kv("Issued Uops", m_stats.issue.issued_insts, 2);
    print_kv("Retired Uops", m_stats.issue.retired_uops, 2);
    print_kv("Flushed Uops", m_stats.issue.flushed_uops, 2);
    print_kv("Replayed Uops", m_stats.issue.replayed_uops, 2);
    print_kv("Inflight Uops", m_stats.issue.inflight_uops, 2);
    print_kv("Fu Busy Count", m_stats.issue.fu_busy_count, 2);
    print_kv("Flushed Insts", m_stats.issue.flushed_insts, 2);
    print_kv("No Ready Inst Cycles", m_stats.issue.no_ready_inst_cycles, 2);

    print_section("Commit Unit");
    print_kv("Resolved Insts", m_stats.commit.resolved_insts, 2);
    print_kv("Retired Insts", m_stats.commit.retired_insts, 2);
    print_kv("ROB Empty Cycles", m_stats.commit.rob_empty_cycles, 2);
    print_kv("Head Not Ready Cycles", m_stats.commit.head_not_ready_cycles, 2);
    print_kv("Head Not Ready Load", m_stats.commit.head_not_ready_load, 2);
    print_kv("Head Not Ready Store", m_stats.commit.head_not_ready_store, 2);
    print_kv("Head Not Ready Other", m_stats.commit.head_not_ready_other, 2);

    print_section("Retired By Type");
    const u32 inst_types = static_cast<u32>(InstType::NUM_INST_TYPES);
    for (u32 t = 0; t < inst_types && t < PerfStats::CommitStats::MAX_INST_TYPES; ++t) {
        const u64 count = m_stats.commit.retired_by_type[t];
        if (count == 0) {
            continue;
        }
        print_kv(inst_type_str(static_cast<InstType>(t)), count, 2);
    }

    print_section("Bubble Slots");
    for (u32 c = 1; c < static_cast<u32>(BubbleCause::COUNT); ++c) {
        print_kv(bubble_cause_name(static_cast<BubbleCause>(c)),
                 m_stats.bubble_slots[c], 2);
    }

    print_section("Branch Predictor Speculation");
    print_branch_predictor_info(m_stats);

    print_section("Flush Unit");
    print_kv("Requests", m_stats.flush.requests, 2);
    print_kv("Events", m_stats.flush.events, 2);
    print_flush_type_table(m_stats);

    print_section("Function Units");
    print_fu_table(m_stats);

    auto print_cache = [](const char* name, const CacheStats& stats) {
        print_section(name);
        print_kv("Accesses", stats.accesses, 2);
        print_kv("Hits", stats.hits, 2);
        print_kv("Misses", stats.misses, 2);
        const f64 hit_rate = stats.accesses == 0
            ? 0.0
            : static_cast<f64>(stats.hits) / static_cast<f64>(stats.accesses);
        print_pct("Hit Rate", hit_rate, 2);
        const f64 avg_lat = stats.accesses == 0
            ? 0.0
            : static_cast<f64>(stats.total_latency) / static_cast<f64>(stats.accesses);
        print_kv("Avg Latency", avg_lat, 2);
    };
    print_cache("ICache", m_stats.icache);
    print_cache("DCache", m_stats.dcache);
    print_cache("ITLB", m_stats.itlb);
    print_cache("DTLB", m_stats.dtlb);

    print_section("Load Replay");
    print_kv("Store Buffer Wait", m_stats.load_replay_sb, 2);
    print_kv("Alias", m_stats.load_replay_alias, 2);
    return;
}

void Topdown::print_results()
{
    std::cout << "Topdown Results\n";
    print_kv("Total Cycles", m_res.total_cycles);
    print_kv("Total Slots", m_res.total_slots);
    print_kv("Issue Width", static_cast<u64>(m_res.issue_width));
    print_kv("Total Insts", m_res.total_insts);
    print_kv("Inflight Uops", m_res.inflight_uops);
    print_kv("IPC", m_res.ipc);

    print_section("Top Level");
    print_pct("Retiring", m_res.retiring, 2);
    print_pct("Bad Speculation", m_res.bad_speculation, 2);
    print_pct("Frontend Bound", m_res.frontend_bound, 2);
    print_pct("Backend Bound", m_res.backend_bound, 2);
    print_pct("Unaccounted", m_res.unaccounted, 2);

    print_section("Bad Speculation");
    print_kv("Branch Mispredict Slots", m_res.branch_mispredict_slots, 2);
    print_kv("Load Replay Slots", m_res.load_replay_slots, 2);
    print_kv("Memory Order Violation Slots", m_res.memory_order_violation_slots, 2);
    print_kv("Other Flush Slots", m_res.other_flush_slots, 2);
    print_pct("Branch Mispredict", m_res.branch_mispredict, 2);
    print_pct("Load Replay", m_res.load_replay, 2);

    print_section("Frontend Bound");
    print_pct("Frontend Latency", m_res.frontend_latency, 2);
    print_pct("Frontend Bandwidth", m_res.frontend_bandwidth, 2);

    print_section("Backend Bound");
    print_kv("MemStalls.AnyLoad", m_res.mem_stall_any_load, 2);
    print_kv("MemStalls.Stores", m_res.mem_stall_stores, 2);
    print_pct("Memory Bound", m_res.memory_bound, 2);
    print_pct("Core Bound", m_res.core_bound, 2);
}

void Topdown::report()
{
    print_results();
    print_stats();
}
