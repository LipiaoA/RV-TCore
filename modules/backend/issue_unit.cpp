#include "issue_unit.h"
#include "isa/opcode.h"
#include <algorithm>
#include <unordered_set>
#include "execute/execute_unit.h"
#include "execute/lsu/lsu.h"
#include "regfile_unit.h"
#include "perf/perf_def.h"

namespace {

bool src_matches_ptag(const Operand& src, i32 ptag)
{
    return src.valid && src.rtyp == RegType::GPR && ptag >= 0 && src.ptag == ptag;
}

} // namespace

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rob(*deps.rob),
      m_exec(*deps.exec),
      m_rename_issue_q(*deps.rename_issue_q),
      m_flush_event(*deps.flush_event),
      m_reg_req_q(*deps.reg_req_q),
      m_stats(*deps.perf_stats),
      m_int_issue_width(conf.int_issue_width),
      m_ls_issue_width(conf.ls_issue_width),
      m_fp_issue_width(conf.fp_issue_width),
      m_vec_issue_width(conf.vec_issue_width),
      m_tile_issue_width(conf.tile_issue_width),
      m_int_queue_size(conf.int_queue_size),
      m_ls_queue_size(conf.ls_queue_size),
      m_fp_queue_size(conf.fp_queue_size),
      m_vec_queue_size(conf.vec_queue_size),
      m_tsr_queue_size(conf.tsr_queue_size),
      m_vec_reg_req_q(*deps.vec_reg_req_q),
      m_tile_reg_req_q(*deps.tile_reg_req_q),
      m_wakeup_bus(*deps.wakeup_bus),
      m_regfile(deps.regfile),
      m_lsu(deps.lsu)
{}

void IssueUnit::build() {
}

void IssueUnit::tick() {
    /* run at previous clock cycle */
    flush();
    async();

    /* run at current clock cycle */
    issue();
    select();

    wakeup();
}

void IssueUnit::commit()
{
    m_reg_req_q.commit();
    m_vec_reg_req_q.commit();
    m_tile_reg_req_q.commit();
}

void IssueUnit::apply_replay_to_inst(MInstPtr inst, i32 ptag, bool kill_sta, bool kill_std)
{
    if (!inst || !inst->valid) {
        return;
    }

    if (inst->type == InstType::STORE) {
        if (kill_sta) {
            inst->sta_iss = false;
            inst->sta_done = false;
            inst->addr_ready = false;
        }
        if (kill_std) {
            inst->std_iss = false;
            inst->std_done = false;
            inst->data_ready = false;
        }
        inst->store_issue_half = StoreIssueHalf::NONE;
        inst->state = InstState::REPLAY;
        m_exec.squash_rid(inst->rid, ptag);
        m_rob.mark_not_ready(inst->rid);
        if (m_lsu) {
            m_lsu->replay_store_half(inst->rid, kill_sta, kill_std);
        }
        return;
    }

    if (ptag >= 0) {
        bool hit = false;
        if (src_matches_ptag(inst->rs1, ptag)) {
            inst->rs1.ready = false;
            inst->rs1.bypass_hint = false;
            inst->rs1.bypass_ready = false;
            inst->rs1.data_available = false;
            inst->rs1.speculative_child_mask = 0;
            hit = true;
        }
        if (src_matches_ptag(inst->rs2, ptag)) {
            inst->rs2.ready = false;
            inst->rs2.bypass_hint = false;
            inst->rs2.bypass_ready = false;
            inst->rs2.data_available = false;
            inst->rs2.speculative_child_mask = 0;
            hit = true;
        }
        if (!hit) {
            return;
        }
    }

    inst->state = InstState::REPLAY;
    m_exec.squash_rid(inst->rid, ptag);
    m_rob.mark_not_ready(inst->rid);
    // if (m_regfile && inst->rd.valid && inst->rd.ptag >= 0) {
    //     m_regfile->set_busy(inst->rd.ptag);
    // }
}

void IssueUnit::handle_dep_kill(const WakeupEvent& event)
{
    std::vector<i32> killed_ptags_vec;
    if (event.ptag >= 0) {
        killed_ptags_vec.push_back(event.ptag);
    }

    auto replay_iq_inst = [&](MInstPtr inst, bool& grew) {
        if (!inst || !inst->valid) {
            return;
        }
        std::deque<i32> kill_ptags(killed_ptags_vec.begin(), killed_ptags_vec.end());
        while (!kill_ptags.empty()) {
            i32 ptag = kill_ptags.front();
            kill_ptags.pop_front();
            const bool hit_rs1 = src_matches_ptag(inst->rs1, ptag);
            const bool hit_rs2 = src_matches_ptag(inst->rs2, ptag);
            if (!hit_rs1 && !hit_rs2) {
                const bool mask_hit =
                    event.speculative_mask != 0 &&
                    ((inst->rs1.speculative_child_mask & event.speculative_mask) != 0 ||
                     (inst->rs2.speculative_child_mask & event.speculative_mask) != 0);
                if (!mask_hit) {
                    continue;
                }
                assert(false);
                inst->state = InstState::REPLAY;
                m_exec.squash_rid(inst->rid, ptag);
                m_rob.mark_not_ready(inst->rid);
                if (m_regfile && inst->rd.valid && inst->rd.ptag >= 0) {
                    m_regfile->set_busy(inst->rd.ptag);
                    if (std::find(killed_ptags_vec.begin(), killed_ptags_vec.end(), inst->rd.ptag) == killed_ptags_vec.end()) {
                        kill_ptags.push_back(inst->rd.ptag);
                        killed_ptags_vec.push_back(inst->rd.ptag);
                        grew = true;
                    }
                }
                continue;
            }
            apply_replay_to_inst(inst, ptag, hit_rs1, hit_rs2);

            if (inst->rd.valid) {
                if (std::find(killed_ptags_vec.begin(), killed_ptags_vec.end(),
                    inst->rd.ptag) == killed_ptags_vec.end()) {
                    grew = true;
                    kill_ptags.push_back(inst->rd.ptag);
                    killed_ptags_vec.push_back(inst->rd.ptag);
                }
            }
        }
    };

    bool grew = true;
    while (grew) {
        grew = false;
        for (auto inst : m_int_q) {
            replay_iq_inst(inst, grew);
        }
        for (auto inst : m_ls_q) {
            replay_iq_inst(inst, grew);
        }
        for (auto inst : m_fp_q) {
            replay_iq_inst(inst, grew);
        }
        for (auto inst : m_vec_q) {
            replay_iq_inst(inst, grew);
        }
        for (auto inst : m_tensor_q) {
            replay_iq_inst(inst, grew);
        }
        for (auto& cand : m_select_buffer) {
            replay_iq_inst(cand.inst, grew);
        }

        // ROB 扫描闭包，仅回灌不在 IQ 的
        std::vector<MInstPtr> to_requeue;
        m_rob.for_each([&](RobEntry& entry) {
            MInstPtr inst = entry.inst;
            if (!inst || !inst->valid) {
                return;
            }
            if (inst->state == InstState::RETIRED || inst->state == InstState::FLUSHED) {
                return;
            }
            bool hit = false;
            bool kill_sta = false;
            bool kill_std = false;
            for (i32 ptag : killed_ptags_vec) {
                if (src_matches_ptag(inst->rs1, ptag)) {
                    hit = true;
                    kill_sta = true;
                }
                if (src_matches_ptag(inst->rs2, ptag)) {
                    hit = true;
                    kill_std = true;
                }
            }
            if (!hit) {
                return;
            }
            if (inst->type == InstType::STORE) {
                apply_replay_to_inst(inst, -1, kill_sta, kill_std);
            } else {
                for (i32 ptag : killed_ptags_vec) {
                    if (src_matches_ptag(inst->rs1, ptag) || src_matches_ptag(inst->rs2, ptag)) {
                        apply_replay_to_inst(inst, ptag, false, false);
                    }
                }
            }
            if (inst->rd.valid) {
                if (std::find(killed_ptags_vec.begin(), killed_ptags_vec.end(),
                    inst->rd.ptag) == killed_ptags_vec.end()) {
                    if (m_regfile) {
                        m_regfile->set_busy(inst->rd.ptag);
                    }
                    grew = true;
                }
            }
            if (!in_issue_q(inst->rid)) {
                to_requeue.push_back(inst);
            }
        });
        for (auto inst : to_requeue) {
            requeue_from_rob(inst);
        }
    }
}

void IssueUnit::wakeup() {
    auto apply_gpr = [](MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid || event.rtyp != RegType::GPR) {
            return;
        }
        if (event.type == WAKEUP_REPLAY || event.load_dep_kill) {
            return;
        }
        auto apply_src = [&](Operand& src) {
            if (!src.valid || src.rtyp != RegType::GPR) {
                return;
            }
            if (!src.ready && (event.type == WAKEUP_BYPASS || event.type == WAKEUP_WRITEBACK)
                && src.ptag == event.ptag) {
                src.ready = true;
                src.bypass_hint = event.type == WAKEUP_BYPASS && event.bypassable;
                src.bypass_ready = false;
                src.speculative_child_mask =
                    event.speculative ? event.speculative_mask : 0;
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

    // 优先处理 replay情况，再处理wakeup
    for (auto& event : m_wakeup_bus) {
        if (!event.valid) {
            continue;
        }
        if (event.type == WAKEUP_REPLAY || event.load_dep_kill) {
            LOG_LEVEL1("IssueUnit", "Replay ptag:p%d rid:%s", event.ptag, event.rid.str());
            handle_dep_kill(event);
        }
    }

    for (const auto& event : m_wakeup_bus) {
        if (!event.valid || event.type == WAKEUP_REPLAY || event.load_dep_kill) {
            continue;
        }
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
        for (auto& cand : m_select_buffer) {
            apply_gpr(cand.inst, event);
        }
    }
    m_wakeup_bus.clear();
}

static bool int_type(InstType type)
{
    return type == InstType::INT;
}

static bool branch_type(InstType type)
{
    return type == InstType::BRANCH;
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

static bool selectable_state(const MInstPtr inst)
{
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->type == InstType::STORE) {
        if (inst->state == InstState::EXECUTED ||
            inst->state == InstState::FLUSHED ||
            inst->state == InstState::RETIRED) {
            return false;
        }
        // 半边未发完前保持可选（state 在双半边都发完前保持 RENAMED/REPLAY）
        return !inst->store_fully_issued() &&
               (inst->state == InstState::RENAMED || inst->state == InstState::REPLAY);
    }
    return inst->state == InstState::RENAMED || inst->state == InstState::REPLAY;
}

void IssueUnit::select()
{
    const size_t int_slots_left =  m_int_issue_width;
    const size_t ls_slots_left = m_ls_issue_width;
    const size_t fp_slots_left = m_fp_issue_width;
    const size_t vec_slots_left = m_vec_issue_width;
    const size_t tile_slots_left = m_tile_issue_width;

    std::vector<IssueCand> candidates;
    candidates.reserve(m_int_q.size() + m_ls_q.size() * 2 + m_fp_q.size()
                       + m_vec_q.size() + m_tensor_q.size());

    for (const auto& inst : m_int_q) {
        if (inst->ready() && selectable_state(inst)) {
            candidates.push_back({inst, StoreIssueHalf::NONE});
        }
    }
    for (const auto& inst : m_ls_q) {
        if (!selectable_state(inst)) {
            continue;
        }
        if (inst->type == InstType::STORE) {
            if (!inst->sta_iss && inst->sta_ready()) {
                candidates.push_back({inst, StoreIssueHalf::STA});
            }
            if (!inst->std_iss && inst->std_ready()) {
                candidates.push_back({inst, StoreIssueHalf::STD});
            }
        } else if (inst->ready()) {
            candidates.push_back({inst, StoreIssueHalf::NONE});
        }
    }
    for (const auto& inst : m_fp_q) {
        if (inst->ready() && selectable_state(inst)) {
            candidates.push_back({inst, StoreIssueHalf::NONE});
        }
    }
    for (const auto& inst : m_vec_q) {
        if (inst->ready() && selectable_state(inst)) {
            candidates.push_back({inst, StoreIssueHalf::NONE});
        }
    }
    for (const auto& inst : m_tensor_q) {
        if (inst->ready() && selectable_state(inst)) {
            candidates.push_back({inst, StoreIssueHalf::NONE});
        }
    }
    if (candidates.empty()) {
        return;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const IssueCand& lhs, const IssueCand& rhs) {
                         return m_rob.is_older_than(lhs.inst->rid, rhs.inst->rid);
                     });
    size_t sel = 0;
    for (size_t i = 0; sel < int_slots_left && i < candidates.size(); ++i) {
        auto& cand = candidates[i];
        if (int_type(cand.inst->type) || branch_type(cand.inst->type)) {
            m_select_buffer.emplace_back(cand);
        if (m_stats.perf()) {
            ++m_stats.issue.selected_insts;
        }
        ++sel;
        }
    }
    sel = 0;
    for (size_t i = 0; sel < ls_slots_left && i < candidates.size(); ++i) {
        auto& cand = candidates[i];
        if (!ls_type(cand.inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(cand);
        if (m_stats.perf()) {
            ++m_stats.issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < fp_slots_left && i < candidates.size(); ++i) {
        auto& cand = candidates[i];
        if (!fp_type(cand.inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(cand);
        if (m_stats.perf()) {
            ++m_stats.issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < vec_slots_left && i < candidates.size(); ++i) {
        auto& cand = candidates[i];
        if (!vec_type(cand.inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(cand);
        if (m_stats.perf()) {
            ++m_stats.issue.selected_insts;
        }
        ++sel;
    }
    sel = 0;
    for (size_t i = 0; sel < tile_slots_left && i < candidates.size(); ++i) {
        auto& cand = candidates[i];
        if (!tensor_type(cand.inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(cand);
        if (m_stats.perf()) {
            ++m_stats.issue.selected_insts;
        }
        ++sel;
    }
}

void IssueUnit::issue()
{
    u64 fu_busy = 0;
    u32 int_issued = 0, ls_issued = 0;
    std::string st_half = "None";

    retire_executed(m_int_q);
    retire_executed(m_ls_q);
    retire_executed(m_fp_q);
    retire_executed(m_vec_q);
    retire_executed(m_tensor_q);
    if ((!m_int_q.empty() && !m_ls_q.empty() && m_fp_q.empty() && m_select_buffer.empty())) {
        if (m_stats.perf()) {
            ++m_stats.issue.no_ready_inst_cycles;
        }
    }

    for (auto& cand : m_select_buffer) {
        auto& inst = cand.inst;
        if (!inst || !inst->valid) {
            continue;
        }
        if (inst->type == InstType::STORE) {
            if (cand.store_half == StoreIssueHalf::STA && !inst->sta_ready()) {
                continue;
            }
            if (cand.store_half == StoreIssueHalf::STD && !inst->std_ready()) {
                continue;
            }
        } else if (!inst->ready()) {
            continue;
        }
        st_half = "None";
        inst->store_issue_half = cand.store_half;
        bool ret = m_exec.dispatch(inst);
        if (m_stats.perf() && ret) {
            ++m_stats.issue.issued_insts;
            if (inst->fetch_id == m_stats.frontend.fetch_id) {
                m_stats.frontend.fetch_id = 0;
            }

        }
        if (!ret) {
            inst->store_issue_half = StoreIssueHalf::NONE;
            ++fu_busy;
        } else {
            if (int_type(inst->type) || branch_type(inst->type)) {
                ++int_issued;
            } else if (ls_type(inst->type)) {
                ++ls_issued;
            }
            if (inst->type == InstType::STORE) {
                if (cand.store_half == StoreIssueHalf::STA) {
                    st_half = "STA";
                    inst->sta_iss = true;
                } else if (cand.store_half == StoreIssueHalf::STD) {
                    st_half = "STD";
                    inst->std_iss = true;
                }
                if (inst->store_fully_issued()) {
                    inst->state = InstState::ISSUED;
                }
                // else if (inst->state != InstState::REPLAY) {
                //     inst->state = InstState::RENAMED;
                // }
            } else {
                inst->state = InstState::ISSUED;
            }
        }

        if (ret) {
            if (inst->type == InstType::VECTOR) {
                EmitVecSrcRead(inst, inst->vs1, 1);
                EmitVecSrcRead(inst, inst->vs2, 2);
                if (inst->rs1.valid) {
                    EmitSrcRead(inst, inst->rs1, 3);
                }
            } else if (inst->type == InstType::TENSOR) {
                EmitTileSrcRead(inst, inst->ts1, 1);
                EmitTileSrcRead(inst, inst->ts2, 2);
            } else if (inst->type == InstType::STORE) {
                if (cand.store_half == StoreIssueHalf::STA) {
                    EmitSrcRead(inst, inst->rs1, 1);
                } else if (cand.store_half == StoreIssueHalf::STD) {
                    EmitSrcRead(inst, inst->rs2, 1);
                }
            } else if (inst->type == InstType::INT ||
                       inst->type == InstType::BRANCH ||
                       inst->type == InstType::FP ||
                       inst->type == InstType::LOAD ||
                       inst->type == InstType::STORE) {
                EmitSrcRead(inst, inst->rs1, 1);
                EmitSrcRead(inst, inst->rs2, 2);
            } else {
                assert(false);
            }
        }

        LOG_LEVEL1("IssueUnit iss", "inst typ:%s half:%s issued:%s pc:0x%lx, name:%s,"
                    "rs1:v%d-x%d-p%d bps:%d, rs2:v%d-x%d-p%d bps:%d, rd:v%d-x%d-p%d,rid:%s",
                    inst_type_str(inst->type), st_half.c_str(),
                    ret ? "true" : "false", inst->pc, inst->inst_name.c_str(),
                    inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.bypass_hint,
                    inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.bypass_hint,
                    inst->rd.valid, inst->rd.atag, inst->rd.ptag,
                    inst->rid.str());
    }
    m_select_buffer.clear();
    clean_bypass_hint();
    if (m_stats.perf()) {
        const FuStates& fu_states = m_exec.get_fu_states();
        u32 i_empty = m_int_issue_width - int_issued + m_ls_issue_width - ls_issued;
        u32 e_empty = fu_states.alu_aval + fu_states.bru_aval + fu_states.agu_aval + fu_states.std_aval;

        m_stats.frontend.fetch_bubbles += std::min(i_empty, e_empty);

        if (m_stats.frontend.fetch_id != 0) {
            m_stats.frontend.recovery_slots += std::min(i_empty, e_empty);
        }
    }
}

void IssueUnit::retire_executed(std::vector<MInstPtr>& inst_q)
{
    std::vector<MInstPtr> tmp_q;
    for (auto& inst : inst_q) {
        if (!inst || !inst->valid) {
            continue;
        }
        // 7A: 仅 EXECUTED 后出队
        if (inst->state == InstState::EXECUTED) {
            continue;
        }
        tmp_q.emplace_back(inst);
    }
    inst_q.swap(tmp_q);
}

void IssueUnit::clean_bypass_hint()
{
    auto cln = [](std::vector<MInstPtr>& q) {
        for (auto& inst : q) {
            if (!inst->valid ||
                (inst->state != InstState::RENAMED &&
                inst->state != InstState::REPLAY)) {
                continue;
            }
            inst->rs1.bypass_hint = false;
            inst->rs2.bypass_hint = false;
            inst->rs1.bypass_ready = false;
            inst->rs2.bypass_ready = false;
            inst->rs1.speculative_child_mask = 0;
            inst->rs2.speculative_child_mask = 0;
        }
    };
    cln(m_int_q);
    cln(m_ls_q);
    // TODO: clean bypass hint for fp, vec, tensor
    return;
}

bool IssueUnit::in_issue_q(const ROBID& rid) const
{
    auto has = [&](const std::vector<MInstPtr>& q) {
        for (auto inst : q) {
            if (inst && inst->rid == rid) {
                return true;
            }
        }
        return false;
    };
    return has(m_int_q) || has(m_ls_q) || has(m_fp_q) || has(m_vec_q) || has(m_tensor_q);
}

void IssueUnit::requeue_from_rob(MInstPtr inst)
{
    if (!inst || !inst->valid || in_issue_q(inst->rid)) {
        return;
    }
    inst->state = InstState::REPLAY;
    inst->store_issue_half = StoreIssueHalf::NONE;
    if (inst->type == InstType::VECTOR) {
        m_vec_q.emplace_back(inst);
    } else if (inst->type == InstType::TENSOR) {
        m_tensor_q.emplace_back(inst);
    } else if (ls_type(inst->type)) {
        m_ls_q.emplace_back(inst);
    } else if (inst->type == InstType::FP) {
        m_fp_q.emplace_back(inst);
    } else {
        m_int_q.emplace_back(inst);
    }
    LOG_LEVEL1("IssueUnit", "requeue from ROB pc:0x%lx type:%s rid:%s",
               inst->pc, inst_type_str(inst->type),
               inst->rid.str());
}

void IssueUnit::async() {
    MInstPtr inst;
    while (m_rename_issue_q.peek(inst)) {
        bool pop = true;
        if (!inst->valid) {
            m_rename_issue_q.read(inst);
            delete inst;
            continue;
        }
        if (inst->type == InstType::VECTOR && m_vec_q.size() < m_vec_queue_size) {
            m_vec_q.emplace_back(inst);
        } else if (inst->type == InstType::TENSOR && m_tensor_q.size() < m_tsr_queue_size) {
            m_tensor_q.emplace_back(inst);
        } else if ((inst->type == InstType::LOAD ||
            inst->type == InstType::STA ||
            inst->type == InstType::STD ||
            inst->type == InstType::STORE) && m_ls_q.size() < m_ls_queue_size) {
            m_ls_q.emplace_back(inst);
        } else if (inst->type == InstType::FP && m_fp_q.size() < m_fp_queue_size) {
            m_fp_q.emplace_back(inst);
        } else if ((int_type(inst->type) || branch_type(inst->type)) && m_int_q.size() < m_int_queue_size) {
            m_int_q.emplace_back(inst);
        } else {
            pop = false;
        }
        if (pop) {
            m_rename_issue_q.read(inst);
        } else {
            // issue queue is full, break the loop
            break;
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
            if (m_rob.is_older_than(m_flush_event.rid, inst->rid)) {
                inst->valid = false;
                if (m_stats.perf()) {
                    ++m_stats.issue.flushed_insts;
                }
            } else {
                tmp_q.emplace_back(inst);
            }
        }
        q.swap(tmp_q);
        return;
    };

    switch (m_flush_event.type) {
    case FlushType::BRANCH_MISPRED: {
        clear_q(m_int_q);
        clear_q(m_ls_q);
        clear_q(m_fp_q);
        clear_q(m_vec_q);
        clear_q(m_tensor_q);
        {
            std::vector<IssueCand> kept;
            for (auto& cand : m_select_buffer) {
                if (cand.inst && m_rob.is_older_than(cand.inst->rid, m_flush_event.rid)) {
                    kept.push_back(cand);
                } else if (cand.inst) {
                    cand.inst->valid = false;
                }
            }
            m_select_buffer.swap(kept);
        }
        m_rename_issue_q.flush_if([&](const MInstPtr& inst) {
            bool flushed = m_rob.is_older_than(m_flush_event.rid, inst->rid);
            if (flushed && m_stats.perf()) {
                ++m_stats.issue.flushed_insts;
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

FuType IssueUnit::GetFuType(const MInstPtr inst, StoreIssueHalf half)
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
        return FuType::AGU;
    case InstType::STORE:
        return half == StoreIssueHalf::STD ? FuType::STD : FuType::AGU;
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
    FuType fu = GetFuType(inst, inst->store_issue_half);
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
    LOG_LEVEL1("IssueUnit SrcRead", "pc: 0x%lx, inst: %s, src: v%d-a%d-p%d, idx: %d, fu: %s",
               inst->pc, inst->inst_name.c_str(), src.valid, src.atag, src.ptag, idx, FuTypeStr[static_cast<int>(fu)]);
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
