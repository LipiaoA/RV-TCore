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
    auto apply_gpr = [](MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid || event.rtyp != RegType::GPR) {
            return;
        }
        auto apply_src = [&](Operand& src) {
            if (!src.valid || src.rtyp != RegType::GPR) {
                return;
            }
            if ((event.type == WAKEUP_BYPASS || event.type == WAKEUP_WRITEBACK)
                && src.ptag == event.ptag) {
                src.ready = true;
                src.bypass_hint = event.type == WAKEUP_BYPASS && event.bypassable;
                src.bypass_ready = false;
                src.speculative_child_mask =
                    event.speculative ? event.speculative_mask : 0;
            } else if (event.type == WAKEUP_REPLAY) {
                const bool replay_by_mask =
                    event.speculative_mask != 0 &&
                    (src.speculative_child_mask & event.speculative_mask) != 0;
                const bool replay_by_ptag = event.ptag >= 0 && src.ptag == event.ptag;
                if (!replay_by_mask && !replay_by_ptag) {
                    return;
                }
                src.ready = false;
                src.bypass_hint = false;
                src.bypass_ready = false;
                src.data_available = false;
                src.speculative_child_mask = 0;
                inst->state = InstState::REPLAY;
            }
        };
        apply_src(inst->rs1);
        apply_src(inst->rs2);
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
        for (auto inst : m_tensor_q) {
            apply_tile(inst, event);
        }
        for (auto inst : m_select_buffer) {
            apply_gpr(inst, event);
        }
    }
    m_wakeup_bus.clear();
}

static bool int_type(InstType type)
{
    return type == InstType::INT || type == InstType::BRANCH;
}

static bool ls_type(InstType type)
{
    return type == InstType::LOAD  || type == InstType::STA ||
           type == InstType::STORE || type == InstType::STD;
}

static bool fp_type(InstType type)
{
    return type == InstType::FP;
}

static bool vec_type(InstType type)
{
    return type == InstType::VECTOR;
}

static bool tensor_type(InstType type)
{
    return type == InstType::TENSOR;
}

void IssueUnit::select()
{
    const size_t int_slots_left =  m_int_issue_width;
    const size_t ls_slots_left = m_ls_issue_width;
    const size_t fp_slots_left = m_fp_issue_width;
    const size_t vec_slots_left = m_vec_issue_width;
    const size_t tile_slots_left = m_tile_issue_width;

    std::vector<MInstPtr> candidates;
    candidates.reserve(m_int_q.size() + m_ls_q.size() + m_fp_q.size()
                       + m_vec_q.size() + m_tensor_q.size());

    for (const auto& inst : m_int_q) {
        if (inst->ready() && (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_ls_q) {
        if (inst->ready() && (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_fp_q) {
        if (inst->ready() && (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_vec_q) {
        if (inst->ready() && (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY)) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_tensor_q) {
        if (inst->ready() && (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY)) {
            candidates.emplace_back(inst);
        }
    }
    if (candidates.empty()) {
        return;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const MInstPtr lhs, const MInstPtr rhs) {
                         return m_rob.is_older_than(lhs->rid, rhs->rid);
                     });
    size_t sel = 0;
    for (size_t i = 0; sel < int_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!int_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < ls_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!ls_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < fp_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!fp_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < vec_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!vec_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < tile_slots_left && i < candidates.size(); ++i) {
        auto& inst = candidates[i];
        if (!tensor_type(inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(inst);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++sel;
    }
}

void IssueUnit::issue()
{
    bool fu_busy_cycle = false;

    update_next_ready(m_int_q);
    update_next_ready(m_ls_q);
    update_next_ready(m_fp_q);
    update_next_ready(m_vec_q);
    update_next_ready(m_tensor_q);

    for (auto& inst : m_select_buffer) {
        if (!inst || !inst->valid) {
            continue;
        }
        if (!inst->ready()) {
            continue;
        }
        bool ret = m_exec.dispatch(inst);
        if (m_stats && ret) {
            ++m_stats->issue.issued_insts;
        }
        if (!ret) {
            if (inst->rs1.bypass_hint) {
                inst->rs1.bypass_hint = false;
                inst->rs1.bypass_ready = false;
                inst->rs1.speculative_child_mask = 0;
            }
            if (inst->rs2.bypass_hint) {
                inst->rs2.bypass_hint = false;
                inst->rs2.bypass_ready = false;
                inst->rs2.speculative_child_mask = 0;
            }
            fu_busy_cycle = true;
        } else {
            inst->state = InstState::ISSUED;
        }
        if (inst->type == InstType::VECTOR) {
            EmitVecSrcRead(inst, inst->vs1, 1);
            EmitVecSrcRead(inst, inst->vs2, 2);
            if (inst->rs1.valid) {
                EmitSrcRead(inst, inst->rs1, 3);
            }
        } else if (inst->type == InstType::TENSOR) {
            EmitTileSrcRead(inst, inst->ts1, 1);
            EmitTileSrcRead(inst, inst->ts2, 2);
        } else {
            EmitSrcRead(inst, inst->rs1, 1);
            EmitSrcRead(inst, inst->rs2, 2);
        }

        LOG_LEVEL1("IssueUnit", "inst typ:%s issued:%s pc:0x%lx, name:%s,"
                    "rs1:v%d-x%d-p%d, rs2:v%d-x%d-p%d, rd:v%d-x%d-p%d%s%d",
                    inst_type_str(inst->type), ret ? "true" : "false", inst->pc, inst->inst_name.c_str(),
                    inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag,
                    inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag,
                    inst->rd.valid, inst->rd.atag, inst->rd.ptag,
                    inst->rid.direct ? "--" : "++", inst->rid.idx);
    }
    m_select_buffer.clear();
    if (fu_busy_cycle && m_stats) {
        ++m_stats->issue.fu_busy_stall_cycles;
    }

    // for (auto it = m_int_q.begin(); it != m_int_q.end();) {
    //     if ((*it)->state == InstState::FU_ACTIVE) {
    //         it = m_int_q.erase(it);
    //     } else {
    //         ++it;
    //     }
    // }
    // for (auto it = m_ls_q.begin(); it != m_ls_q.end();) {
    //     if ((*it)->state == InstState::FU_ACTIVE) {
    //         it = m_ls_q.erase(it);
    //     } else {
    //         ++it;
    //     }
    // }
    // for (auto it = m_fp_q.begin(); it != m_fp_q.end();) {
    //     if ((*it)->state == InstState::FU_ACTIVE) {
    //         it = m_fp_q.erase(it);
    //     } else {
    //         ++it;
    //     }
    // }
    // for (auto it = m_vec_q.begin(); it != m_vec_q.end();) {
    //     if ((*it)->state == InstState::FU_ACTIVE) {
    //         it = m_vec_q.erase(it);
    //     } else {
    //         ++it;
    //     }
    // }
    // for (auto it = m_tensor_q.begin(); it != m_tensor_q.end();) {
    //     if ((*it)->state == InstState::FU_ACTIVE) {
    //         it = m_tensor_q.erase(it);
    //     } else {
    //         ++it;
    //     }
    // }
}

void IssueUnit::update_next_ready(std::vector<MInstPtr>& inst_q)
{
    std::vector<MInstPtr> tmp_q;
    for (auto& inst : inst_q) {
        if (!inst || !inst->valid) {
            continue;
        }
        if (inst->state == InstState::ISSUED && !inst->next_valid) {
            inst->next_valid = true;
            tmp_q.emplace_back(inst);
        } else if (inst->state != InstState::ISSUED) {
            tmp_q.emplace_back(inst);
        }
    }
    inst_q.swap(tmp_q);
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
        } else if (inst->type == InstType::TENSOR) {
            m_tensor_q.emplace_back(inst);
        } else if (inst->type == InstType::LOAD ||
            inst->type == InstType::STA ||
            inst->type == InstType::STD) {
            m_ls_q.emplace_back(inst);
        } else if (inst->type == InstType::FP) {
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
        clear_q(m_tensor_q);
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
        m_tensor_q.clear();
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
    case InstType::TENSOR:
        return FuType::TPU;
    default:
        return FuType::NONE;
    }
}

void IssueUnit::EmitSrcRead(const MInstPtr inst, const Operand& src, int idx)
{
    if (!src.valid || src.bypass_hint) {
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
    m_tensor_q.clear();
    m_select_buffer.clear();
    m_wakeup_bus.clear();
    m_reg_req_q.clear();
    m_vec_reg_req_q.clear();
    m_tile_reg_req_q.clear();
}
