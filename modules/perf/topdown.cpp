#include "topdown.h"

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
    oss << std::fixed << std::setprecision(4) << value;
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
        && s.completed_uops == 0 && s.rejected_uops == 0;
}

void print_fu_table(const PerfStats& stats)
{
    const int widths[] = {6, 4, 14, 14, 16, 14};
    const std::string header[] = {
        "Type", "Id", "Accepted Uops", "Flushed Uops", "Completed Uops", "Rejected Uops"
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
                fu_type_name(t),
                fmt_u64(id),
                fmt_u64(fu.accepted_uops),
                fmt_u64(fu.flushed_uops),
                fmt_u64(fu.completed_uops),
                fmt_u64(fu.rejected_uops),
            };
            print_table_row(widths, row, n);
        }
    }
    if (!any) {
        const std::string row[] = {"-", "-", "0", "0", "0", "0"};
        print_table_row(widths, row, n);
    }
    print_table_border(widths, n);
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

    print_section("Rename Unit");
    print_kv("Input Insts", m_stats.rename.input_insts, 2);
    print_kv("Renamed Insts", m_stats.rename.renamed_insts, 2);
    print_kv("Dispatched Insts", m_stats.rename.dispatched_insts, 2);
    print_kv("ROB Full Stall Cycles", m_stats.rename.rob_full_stall_cycles, 2);
    print_kv("Free List Stall Cycles", m_stats.rename.free_list_stall_cycles, 2);
    print_kv("Flushed Insts", m_stats.rename.flushed_insts, 2);

    print_section("Issue Unit");
    print_kv("Selected Insts", m_stats.issue.selected_insts, 2);
    print_kv("Issued Insts", m_stats.issue.issued_insts, 2);
    print_kv("Fu Busy Count", m_stats.issue.fu_busy_count, 2);
    print_kv("Flushed Insts", m_stats.issue.flushed_insts, 2);
    print_kv("No Ready Inst Cycles", m_stats.issue.no_ready_inst_cycles, 2);

    print_section("Commit Unit");
    print_kv("Resolved Insts", m_stats.commit.resolved_insts, 2);
    print_kv("Retired Insts", m_stats.commit.retired_insts, 2);
    print_kv("ROB Empty Cycles", m_stats.commit.rob_empty_cycles, 2);
    print_kv("Head Not Ready Cycles", m_stats.commit.head_not_ready_cycles, 2);

    print_section("Flush Unit");
    print_kv("Requests", m_stats.flush.requests, 2);
    print_kv("Events", m_stats.flush.events, 2);
    print_flush_type_table(m_stats);

    print_section("Function Units");
    print_fu_table(m_stats);
}
