#include "issue_unit.h"
#include "isa/opcode.h"
#include <algorithm>
#include "execute/execute_unit.h"
#include "perf/perf_stats.h"

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rob(*deps.rob),
      m_exec(*deps.exec),
      m_rename_issue_q(*deps.rename_issue_q),
      m_flush_event(*deps.flush_event),
      m_reg_req_q(*deps.reg_req_q),
      m_stats(deps.perf_stats),
      m_int_issue_width(conf.int_issue_width),
      m_ls_issue_width(conf.ls_issue_width),
      m_fp_issue_width(conf.fp_issue_width),
      m_vec_issue_width(conf.vec_issue_width),
      m_tile_issue_width(conf.tile_issue_width),
      m_vec_reg_req_q(*deps.vec_reg_req_q),
      m_tile_reg_req_q(*deps.tile_reg_req_q),
      m_wakeup_bus(*deps.wakeup_bus)
{}

void IssueUnit::build() {
}

void IssueUnit::tick() {
    flush();
    /* run at previous clock cycle */
    async();
    wakeup();

    /* run at current clock cycle */
    issue();
    select();
}

void IssueUnit::commit()
{
    m_reg_req_q.commit();
    m_vec_reg_req_q.commit();
    m_tile_reg_req_q.commit();
}

void IssueUnit::wakeup() {
    auto apply_gpr = [](const MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid || event.rtyp != RegType::GPR ||
            (event.type != WAKEUP_BYPASS && event.type != WAKEUP_WRITEBACK)) {
            return;
        }
        if (inst->rs1.valid && inst->rs1.rtyp == RegType::GPR
            && inst->rs1.ptag == event.ptag) {
            inst->rs1.ready = true;
        }
        if (inst->rs2.valid && inst->rs2.rtyp == RegType::GPR
            && inst->rs2.ptag == event.ptag) {
            inst->rs2.ready = true;
        }
    };
    auto apply_vec = [](const MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid || event.rtyp != RegType::VEC ||
            (event.type != WAKEUP_BYPASS && event.type != WAKEUP_WRITEBACK)) {
            return;
        }
        if (inst->vs1.valid && inst->vs1.ptag == event.ptag) {
            inst->vs1.ready = true;
        }
        if (inst->vs2.valid && inst->vs2.ptag == event.ptag) {
            inst->vs2.ready = true;
        }
        if (inst->vd.valid && inst->vd.ptag == event.ptag) {
            inst->vd.ready = true;
        }
    };
    auto apply_tile = [](const MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid || event.rtyp != RegType::TILE ||
            (event.type != WAKEUP_BYPASS && event.type != WAKEUP_WRITEBACK)) {
            return;
        }
        if (inst->ts1.valid && inst->ts1.ptag == event.ptag) {
            inst->ts1.ready = true;
        }
        if (inst->ts2.valid && inst->ts2.ptag == event.ptag) {
            inst->ts2.ready = true;
        }
        if (inst->td.valid && inst->td.ptag == event.ptag) {
            inst->td.ready = true;
        }
    };

    for (const auto& event : m_wakeup_bus) {
        for (auto inst : m_int_q) {
            apply_gpr(inst, event);
        }
        for (auto inst : m_ls_q) {
            apply_gpr(inst, event);
        }
        for (auto inst : m_fp_q) {
            apply_gpr(inst, event);
        }
        for (auto inst : m_vec_q) {
            apply_gpr(inst, event);
            apply_vec(inst, event);
        }
        for (auto inst : m_tile_q) {
            apply_tile(inst, event);
        }
    }
    m_wakeup_bus.clear();
}

static bool is_int_issue_type(InstType type)
{
    return type == InstType::INT || type == InstType::BRANCH;
}

static bool is_ls_issue_type(InstType type)
{
    return type == InstType::LOAD || type == InstType::STA || type == InstType::STORE
        || type == InstType::STD;
}

static bool is_fp_issue_type(InstType type)
{
    return type == InstType::FP;
}

static bool is_vec_issue_type(InstType type)
{
    return type == InstType::VECTOR;
}

static bool is_tile_issue_type(InstType type)
{
    return type == InstType::TILE;
}

bool IssueUnit::is_buffered(const MInstPtr& inst) const
{
    for (const auto& pending : m_select_buffer) {
        if (pending == inst) {
            return true;
        }
    }
    return false;
}

size_t IssueUnit::count_buffered_int_slots() const
{
    size_t n = 0;
    for (const auto& inst : m_select_buffer) {
        if (inst && is_int_issue_type(inst->type)) {
            ++n;
        }
    }
    return n;
}

size_t IssueUnit::count_buffered_ls_slots() const
{
    size_t n = 0;
    for (const auto& inst : m_select_buffer) {
        if (inst && is_ls_issue_type(inst->type)) {
            ++n;
        }
    }
    return n;
}

void IssueUnit::select()
{
    const size_t int_slots_left =
        m_int_issue_width > count_buffered_int_slots()
            ? m_int_issue_width - count_buffered_int_slots()
            : 0;
    const size_t ls_slots_left =
        m_ls_issue_width > count_buffered_ls_slots()
            ? m_ls_issue_width - count_buffered_ls_slots()
            : 0;
    size_t buffered_fp_slots = 0;
    for (const auto& inst : m_select_buffer) {
        if (inst && is_fp_issue_type(inst->type)) {
            ++buffered_fp_slots;
        }
    }
    const size_t fp_slots_left =
        m_fp_issue_width > buffered_fp_slots
            ? m_fp_issue_width - buffered_fp_slots
            : 0;
    size_t buffered_vec_slots = 0;
    size_t buffered_tile_slots = 0;
    for (const auto& inst : m_select_buffer) {
        if (inst && is_vec_issue_type(inst->type)) {
            ++buffered_vec_slots;
        }
        if (inst && is_tile_issue_type(inst->type)) {
            ++buffered_tile_slots;
        }
    }
    const size_t vec_slots_left =
        m_vec_issue_width > buffered_vec_slots
            ? m_vec_issue_width - buffered_vec_slots
            : 0;
    const size_t tile_slots_left =
        m_tile_issue_width > buffered_tile_slots
            ? m_tile_issue_width - buffered_tile_slots
            : 0;
    if ((int_slots_left == 0 && ls_slots_left == 0 && fp_slots_left == 0
         && vec_slots_left == 0 && tile_slots_left == 0) ||
        (m_int_q.empty() && m_ls_q.empty() && m_fp_q.empty()
         && m_vec_q.empty() && m_tile_q.empty())) {
        return;
    }

    std::vector<MInstPtr> candidates;
    candidates.reserve(m_int_q.size() + m_ls_q.size() + m_fp_q.size()
                       + m_vec_q.size() + m_tile_q.size());

    for (const auto& inst : m_int_q) {
        if (inst->ready() && !is_buffered(inst)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_ls_q) {
        if (inst->ready() && !is_buffered(inst)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_fp_q) {
        if (inst->ready() && !is_buffered(inst)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_vec_q) {
        if (inst->ready() && !is_buffered(inst)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_tile_q) {
        if (inst->ready() && !is_buffered(inst)) {
            candidates.emplace_back(inst);
        }
    }
    if (candidates.empty()) {
        if (m_stats && m_select_buffer.empty()) {
            ++m_stats->issue.ready_stall_cycles;
        }
        return;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const MInstPtr lhs, const MInstPtr rhs) {
                         return m_rob.is_older_than(lhs->rid, rhs->rid);
                     });
    size_t sel = 0;
    for (size_t i = 0; sel < int_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!is_int_issue_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        EmitSrcRead(inst, inst->rs1, 1);
        EmitSrcRead(inst, inst->rs2, 2);
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < ls_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!is_ls_issue_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        EmitSrcRead(inst, inst->rs1, 1);
        EmitSrcRead(inst, inst->rs2, 2);
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < fp_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!is_fp_issue_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        EmitSrcRead(inst, inst->rs1, 1);
        EmitSrcRead(inst, inst->rs2, 2);
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < vec_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!is_vec_issue_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        EmitVecSrcRead(inst, inst->vs1, 1);
        EmitVecSrcRead(inst, inst->vs2, 2);
        if (inst->rs1.valid && inst->rs1.rtyp == RegType::GPR) {
            EmitSrcRead(inst, inst->rs1, 3);
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < tile_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!is_tile_issue_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        EmitTileSrcRead(inst, inst->ts1, 1);
        EmitTileSrcRead(inst, inst->ts2, 2);
        ++sel;
    }
}

void IssueUnit::issue()
{
    std::vector<MInstPtr> retry;
    retry.reserve(m_select_buffer.size());
    bool fu_busy_cycle = false;

    for (auto& inst : m_select_buffer) {
        if (!inst || !inst->valid) {
            continue;
        }
        bool ret = m_exec.dispatch(inst);
        if (m_stats && ret) {
            ++m_stats->issue.issued_insts;
        }
        if (!ret) {
            retry.emplace_back(inst);
            fu_busy_cycle = true;
        }
        LOG_LEVEL1("IssueUnit", "issued:%s pc:0x%lx, %s, v%d-x%d-p%d%s%d", ret ? "true" : "false", inst->pc,
                   inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
                   inst->rd.ptag, inst->rid.idx == 0 ? "++" : "--", inst->rid.idx);
    }
    m_select_buffer.swap(retry);
    if (fu_busy_cycle && m_stats) {
        ++m_stats->issue.fu_busy_stall_cycles;
    }

    for (auto it = m_int_q.begin(); it != m_int_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_int_q.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_ls_q.begin(); it != m_ls_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_ls_q.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_fp_q.begin(); it != m_fp_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_fp_q.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_vec_q.begin(); it != m_vec_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_vec_q.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_tile_q.begin(); it != m_tile_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_tile_q.erase(it);
        } else {
            ++it;
        }
    }
}

void IssueUnit::async() {
    MInstPtr inst;
    while (m_rename_issue_q.read(inst)) {
        if (!inst->valid) {
            delete inst;
            continue;
        }
        InstOp op = inst->opcode;
        if (inst->type == InstType::VECTOR) {
            m_vec_q.emplace_back(inst);
        } else if (inst->type == InstType::TILE) {
            m_tile_q.emplace_back(inst);
        } else if (is_load_store(op)) {
            m_ls_q.emplace_back(inst);
        } else if (is_fp(op)) {
            m_fp_q.emplace_back(inst);
        } else {
            m_int_q.emplace_back(inst);
        }
        if (m_stats) {
            ++m_stats->issue.enqueued_insts;
        }
    }
    return;
}

void IssueUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    auto clear_q = [&](std::vector<MInstPtr>& q) {
        std::vector<MInstPtr> tmp_q;
        for (auto inst : q) {
            if (m_rob.is_older_than(inst->rid, m_flush_event.rid)) {
                tmp_q.emplace_back(inst);
            } else {
                inst->valid = false;
                if (m_stats) {
                    ++m_stats->issue.flushed_insts;
                }
            }
        }
        q.swap(tmp_q);
        return;
    };

    switch (m_flush_event.type) {
    case FlushType::BRANCH_MISPRED: {
        clear_q(m_int_q);
        clear_q(m_ls_q);
        clear_q(m_select_buffer);
        clear_q(m_fp_q);
        clear_q(m_vec_q);
        clear_q(m_tile_q);
        m_rename_issue_q.flush_if([&](const MInstPtr& inst) {
            bool flushed = m_rob.is_older_than(m_flush_event.rid, inst->rid);
            if (flushed && m_stats) {
                ++m_stats->issue.flushed_insts;
            }
            return flushed;
        });
    } break;
    case FlushType::EXCEPTION:
    case FlushType::INTERRUPT:
    case FlushType::DEBUG:
    case FlushType::MEMORY_ORDER:
    case FlushType::LOAD_REPLAY:
    case FlushType::FENCE: {
        m_int_q.clear();
        m_ls_q.clear();
        m_fp_q.clear();
        m_vec_q.clear();
        m_tile_q.clear();
        m_select_buffer.clear();
        m_rename_issue_q.clear();
    } break;
    default:
        assert(0);
    }
    return;
}

FuType IssueUnit::GetFuType(const MInstPtr inst)
{
    switch (inst->type) {
    case InstType::INT:
        return FuType::ALU;
    case InstType::STD:
        return FuType::STD;
    case InstType::BRANCH:
        return FuType::BRU;
    case InstType::LOAD:
    case InstType::STA:
    case InstType::STORE:
        return FuType::AGU;
    case InstType::FP:
        return FuType::FPU;
    case InstType::VECTOR:
        return FuType::VPU;
    case InstType::TILE:
        return FuType::TPU;
    default:
        return FuType::NONE;
    }
}

void IssueUnit::EmitSrcRead(const MInstPtr inst, const Operand& src, int idx)
{
    if (!src.valid) {
        return;
    }
    FuType fu = GetFuType(inst);
    if (fu == FuType::NONE) {
        assert(false);
    }
    RegReq req;
    req.valid = true;
    req.write = false;
    req.idx = idx;
    req.ptag = src.ptag;
    req.atag = src.atag;
    req.rid = inst->rid;
    req.fu = fu;
    req.rtyp = RegType::GPR;
    m_reg_req_q.write(req);
}

void IssueUnit::EmitVecSrcRead(const MInstPtr inst, const Operand& src, int idx)
{
    if (!src.valid) {
        return;
    }
    VecRegReq req;
    req.valid = true;
    req.write = false;
    req.idx = idx;
    req.ptag = src.ptag;
    req.atag = src.atag;
    req.rid = inst->rid;
    req.fu = FuType::VPU;
    m_vec_reg_req_q.write(req);
}

void IssueUnit::EmitTileSrcRead(const MInstPtr inst, const Operand& src, int idx)
{
    if (!src.valid) {
        return;
    }
    TileRegReq req;
    req.valid = true;
    req.write = false;
    req.idx = idx;
    req.ptag = src.ptag;
    req.atag = src.atag;
    req.rid = inst->rid;
    req.fu = FuType::TPU;
    m_tile_reg_req_q.write(req);
}

void IssueUnit::reset() {
    m_int_q.clear();
    m_ls_q.clear();
    m_fp_q.clear();
    m_vec_q.clear();
    m_tile_q.clear();
    m_select_buffer.clear();
    m_wakeup_bus.clear();
    m_reg_req_q.clear();
    m_vec_reg_req_q.clear();
    m_tile_reg_req_q.clear();
}
