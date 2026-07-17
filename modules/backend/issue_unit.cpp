#include "issue_unit.h"
#include "isa/opcode.h"
#include <algorithm>
#include "execute/execute_unit.h"
#include "execute/lsu/lsu.h"
#include "perf/perf_stats.h"

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rob(*deps.rob),
      m_lsu(*deps.lsu),
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
      m_fu_accept_q(*deps.fu_accept_q),
      m_wakeup_bus(*deps.wakeup_bus)
{}

void IssueUnit::build() {
}

void IssueUnit::tick() {
    flush();
    /* run at previous clock cycle */
    async();
    wakeup();
    handle_fu_feedback();

    /* run at current clock cycle */
    issue();
    select();
}

void IssueUnit::commit()
{
    m_reg_req_q.commit();
    m_vec_reg_req_q.commit();
    m_tile_reg_req_q.commit();
    m_fu_accept_q.commit();
}

void IssueUnit::apply_gpr_wakeup(
    MInstPtr inst,
    const WakeupEvent& event)
{
    if (!inst || !event.valid || event.rtyp != RegType::GPR) {
        return;
    }
    auto apply_src = [&](Operand& src) {
        if (!src.valid || src.rtyp != RegType::GPR) {
            return;
        }
        if ((event.type == WAKEUP_BYPASS ||
             event.type == WAKEUP_WRITEBACK) &&
            src.ptag == event.ptag) {
            src.ready = true;
            src.bypass_hint =
                event.type == WAKEUP_BYPASS && event.bypassable;
            src.bypass_ready = false;
            src.speculative_child_mask =
                event.speculative ? event.speculative_mask : 0;
            return;
        }
        if (event.type != WAKEUP_REPLAY ||
            inst->type == InstType::STORE) {
            return;
        }
        bool by_mask = event.speculative_mask != 0 &&
            (src.speculative_child_mask & event.speculative_mask) != 0;
        bool by_ptag = event.ptag >= 0 && src.ptag == event.ptag;
        if (by_mask || by_ptag) {
            src.ready = false;
            src.bypass_hint = false;
            src.bypass_ready = false;
            src.data_available = false;
            src.speculative_child_mask = 0;
            inst->fu_pending = false;
            inst->state = InstState::REPLAY;
        }
    };
    apply_src(inst->rs1);
    apply_src(inst->rs2);
}

void IssueUnit::apply_vec_wakeup(
    MInstPtr inst,
    const WakeupEvent& event)
{
    if (!inst || !event.valid || event.rtyp != RegType::VEC ||
        (event.type != WAKEUP_BYPASS &&
         event.type != WAKEUP_WRITEBACK)) {
        return;
    }
    if (inst->vs1.valid && inst->vs1.ptag == event.ptag)
        inst->vs1.ready = true;
    if (inst->vs2.valid && inst->vs2.ptag == event.ptag)
        inst->vs2.ready = true;
    if (inst->vd.valid && inst->vd.ptag == event.ptag)
        inst->vd.ready = true;
}

void IssueUnit::apply_tile_wakeup(
    MInstPtr inst,
    const WakeupEvent& event)
{
    if (!inst || !event.valid || event.rtyp != RegType::TILE ||
        (event.type != WAKEUP_BYPASS &&
         event.type != WAKEUP_WRITEBACK)) {
        return;
    }
    if (inst->ts1.valid && inst->ts1.ptag == event.ptag)
        inst->ts1.ready = true;
    if (inst->ts2.valid && inst->ts2.ptag == event.ptag)
        inst->ts2.ready = true;
    if (inst->td.valid && inst->td.ptag == event.ptag)
        inst->td.ready = true;
}

void IssueUnit::wakeup()
{
    for (const auto& event : m_wakeup_bus) {
        handle_non_store_wakeup_replay(event);
        handle_store_wakeup_replay(event);
        for (auto inst : m_int_q) {
            apply_gpr_wakeup(inst, event);
        }
        for (auto inst : m_ls_q) {
            apply_gpr_wakeup(inst, event);
        }
        for (auto inst : m_fp_q) {
            apply_gpr_wakeup(inst, event);
        }
        for (auto inst : m_vec_q) {
            apply_gpr_wakeup(inst, event);
            apply_vec_wakeup(inst, event);
        }
        for (auto inst : m_tensor_q) {
            apply_tile_wakeup(inst, event);
        }
        for (const auto& request : m_select_buffer) {
            apply_gpr_wakeup(request.inst, event);
        }
    }
    m_wakeup_bus.clear();
}

void IssueUnit::handle_non_store_wakeup_replay(
    const WakeupEvent& event)
{
    if (!event.valid || event.type != WAKEUP_REPLAY) {
        return;
    }
    RobEntry* rob_entry = m_rob.get(event.rid);
    MInstPtr inst = rob_entry ? rob_entry->inst : nullptr;
    if (!inst || !inst->valid || inst->type == InstType::STORE) {
        return;
    }
    if (inst->rid.idx != event.rid.idx ||
        inst->rid.direct != event.rid.direct) {
        return;
    }
    if (inst->state != InstState::RENAMED &&
        inst->state != InstState::REPLAY &&
        inst->state != InstState::FU_ACTIVE &&
        inst->state != InstState::ISSUED) {
        return;
    }
    apply_gpr_wakeup(inst, event);
    if (inst->state == InstState::REPLAY) {
        requeue_inst(inst);
    }
}

void IssueUnit::handle_store_wakeup_replay(const WakeupEvent& event)
{
    if (!event.valid || event.type != WAKEUP_REPLAY ||
        event.rtyp != RegType::GPR) {
        return;
    }
    auto matches = [&](const Operand& src) {
        if (!src.valid || src.rtyp != RegType::GPR) {
            return false;
        }
        bool by_mask = event.speculative_mask != 0 &&
            (src.speculative_child_mask & event.speculative_mask) != 0;
        bool by_ptag = event.ptag >= 0 && src.ptag == event.ptag;
        return by_mask || by_ptag;
    };
    for (auto& sq : m_lsu.store_q()) {
        MInstPtr inst = sq.uop;
        if (!sq.valid || !inst || !inst->valid ||
            inst->type != InstType::STORE) {
            continue;
        }
        if (inst->state != InstState::RENAMED &&
            inst->state != InstState::REPLAY &&
            inst->state != InstState::FU_ACTIVE &&
            inst->state != InstState::ISSUED) {
            continue;
        }
        bool replayed = false;
        if (matches(inst->rs1)) {
            ++inst->sta_generation;
            inst->sta_state = StorePartState::WAIT_ISSUE;
            inst->rs1.ready = false;
            inst->rs1.bypass_hint = false;
            inst->rs1.bypass_ready = false;
            inst->rs1.data_available = false;
            inst->rs1.speculative_child_mask = 0;
            m_lsu.replay_store_part(inst, StorePart::STA);
            replayed = true;
        }
        if (matches(inst->rs2)) {
            ++inst->std_generation;
            inst->std_state = StorePartState::WAIT_ISSUE;
            inst->rs2.ready = false;
            inst->rs2.bypass_hint = false;
            inst->rs2.bypass_ready = false;
            inst->rs2.data_available = false;
            inst->rs2.speculative_child_mask = 0;
            m_lsu.replay_store_part(inst, StorePart::STD);
            replayed = true;
        }
        if (replayed) {
            inst->state = InstState::REPLAY;
            inst->next_valid = false;
            requeue_store(inst);
        }
    }
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

bool IssueUnit::store_part_ready(MInstPtr inst, StorePart part) const
{
    if (!inst || inst->type != InstType::STORE) {
        return false;
    }
    if (part == StorePart::STA) {
        return inst->sta_state == StorePartState::WAIT_ISSUE &&
               (!inst->rs1.valid || inst->rs1.ready);
    }
    if (part == StorePart::STD) {
        return inst->std_state == StorePartState::WAIT_ISSUE &&
               (!inst->rs2.valid || inst->rs2.ready);
    }
    return false;
}

bool IssueUnit::request_ready(const IssueRequest& request) const
{
    MInstPtr inst = request.inst;
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->state != InstState::RENAMED &&
        inst->state != InstState::REPLAY) {
        return false;
    }
    if (inst->type == InstType::STORE) {
        u32 generation = request.part == StorePart::STA
            ? inst->sta_generation
            : inst->std_generation;
        if (request.generation != generation) {
            return false;
        }
        return store_part_ready(inst, request.part);
    }
    return !inst->fu_pending && inst->ready();
}

void IssueUnit::collect_store_candidates(
    MInstPtr inst,
    std::vector<IssueRequest>& candidates)
{
    if (store_part_ready(inst, StorePart::STA)) {
        candidates.push_back({inst, StorePart::STA, inst->sta_generation});
    }
    if (store_part_ready(inst, StorePart::STD)) {
        candidates.push_back({inst, StorePart::STD, inst->std_generation});
    }
}

void IssueUnit::collect_candidates(std::vector<IssueRequest>& candidates)
{
    auto collect = [&](const std::vector<MInstPtr>& queue) {
        for (auto* inst : queue) {
            if (!inst || (inst->state != InstState::RENAMED &&
                          inst->state != InstState::REPLAY)) {
                continue;
            }
            if (inst->type == InstType::STORE) {
                collect_store_candidates(inst, candidates);
            } else if (!inst->fu_pending && inst->ready()) {
                candidates.push_back({inst, StorePart::NONE, 0});
            }
        }
    };
    collect(m_int_q);
    collect(m_ls_q);
    collect(m_fp_q);
    collect(m_vec_q);
    collect(m_tensor_q);
}

void IssueUnit::select_candidates(
    const std::vector<IssueRequest>& candidates,
    size_t width,
    bool (*type_match)(InstType))
{
    size_t selected = 0;
    for (const auto& request : candidates) {
        if (selected >= width) {
            break;
        }
        if (!request.inst || !type_match(request.inst->type)) {
            continue;
        }
        m_select_buffer.emplace_back(request);
        if (m_stats) {
            ++m_stats->issue.selected_insts;
        }
        ++selected;
    }
}

void IssueUnit::select()
{
    std::vector<IssueRequest> candidates;
    candidates.reserve(m_int_q.size() + m_ls_q.size() * 2 +
                       m_fp_q.size() + m_vec_q.size() +
                       m_tensor_q.size());
    collect_candidates(candidates);
    if (candidates.empty()) {
        return;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const IssueRequest& lhs,
                            const IssueRequest& rhs) {
                         if (lhs.inst == rhs.inst) {
                             return static_cast<int>(lhs.part) <
                                    static_cast<int>(rhs.part);
                         }
                         return m_rob.is_older_than(
                             lhs.inst->rid, rhs.inst->rid);
                     });
    select_candidates(candidates, m_int_issue_width, int_type);
    select_candidates(candidates, m_ls_issue_width, ls_type);
    select_candidates(candidates, m_fp_issue_width, fp_type);
    select_candidates(candidates, m_vec_issue_width, vec_type);
    select_candidates(candidates, m_tile_issue_width, tensor_type);
}

void IssueUnit::clear_bypass_hint(const IssueRequest& request)
{
    auto clear = [](Operand& src) {
        src.bypass_hint = false;
        src.bypass_ready = false;
        src.speculative_child_mask = 0;
    };
    if (request.part == StorePart::STA) {
        clear(request.inst->rs1);
    } else if (request.part == StorePart::STD) {
        clear(request.inst->rs2);
    } else {
        if (request.inst->rs1.bypass_hint) {
            clear(request.inst->rs1);
        }
        if (request.inst->rs2.bypass_hint) {
            clear(request.inst->rs2);
        }
    }
}

void IssueUnit::mark_dispatched(const IssueRequest& request)
{
    if (request.part == StorePart::STA) {
        request.inst->sta_state = StorePartState::WAIT_ACCEPT;
    } else if (request.part == StorePart::STD) {
        request.inst->std_state = StorePartState::WAIT_ACCEPT;
    } else {
        request.inst->fu_pending = true;
    }
}

void IssueUnit::emit_request_reads(const IssueRequest& request)
{
    MInstPtr inst = request.inst;
    if (request.part == StorePart::STA) {
        EmitSrcRead(inst, inst->rs1, 1, FuType::AGU);
    } else if (request.part == StorePart::STD) {
        EmitSrcRead(inst, inst->rs2, 2, FuType::STD);
    } else if (inst->type == InstType::VECTOR) {
        EmitVecSrcRead(inst, inst->vs1, 1);
        EmitVecSrcRead(inst, inst->vs2, 2);
        EmitSrcRead(inst, inst->rs1, 3);
    } else if (inst->type == InstType::TENSOR) {
        EmitTileSrcRead(inst, inst->ts1, 1);
        EmitTileSrcRead(inst, inst->ts2, 2);
    } else {
        EmitSrcRead(inst, inst->rs1, 1);
        EmitSrcRead(inst, inst->rs2, 2);
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

    for (const auto& request : m_select_buffer) {
        MInstPtr inst = request.inst;
        if (!inst || !inst->valid) {
            continue;
        }
        if (!request_ready(request)) {
            continue;
        }
        bool ret = m_exec.dispatch(request);
        if (m_stats && ret) {
            ++m_stats->issue.issued_insts;
        }
        if (!ret) {
            clear_bypass_hint(request);
            fu_busy_cycle = true;
        } else {
            mark_dispatched(request);
            emit_request_reads(request);
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
}

void IssueUnit::update_next_ready(std::vector<MInstPtr>& inst_q)
{
    std::vector<MInstPtr> tmp_q;
    for (auto& inst : inst_q) {
        if (!inst || !inst->valid) {
            continue;
        }
        if (inst->state != InstState::FU_ACTIVE &&
            inst->state != InstState::ISSUED) {
            tmp_q.emplace_back(inst);
        }
    }
    inst_q.swap(tmp_q);
}

void IssueUnit::requeue_store(MInstPtr inst)
{
    if (!inst || !inst->valid) {
        return;
    }
    auto found = std::find(m_ls_q.begin(), m_ls_q.end(), inst);
    if (found == m_ls_q.end()) {
        m_ls_q.emplace_back(inst);
    }
}

void IssueUnit::requeue_inst(MInstPtr inst)
{
    if (!inst || !inst->valid) {
        return;
    }
    std::vector<MInstPtr>* queue = &m_int_q;
    if (inst->type == InstType::VECTOR) {
        queue = &m_vec_q;
    } else if (inst->type == InstType::TENSOR) {
        queue = &m_tensor_q;
    } else if (inst->type == InstType::LOAD ||
               inst->type == InstType::STA ||
               inst->type == InstType::STD ||
               inst->type == InstType::STORE) {
        queue = &m_ls_q;
    } else if (inst->type == InstType::FP) {
        queue = &m_fp_q;
    }
    if (std::find(queue->begin(), queue->end(), inst) == queue->end()) {
        queue->emplace_back(inst);
    }
}

void IssueUnit::handle_store_feedback(const FuAcceptEvent& event)
{
    MInstPtr inst = event.inst;
    if (!inst || !inst->valid || inst->type != InstType::STORE) {
        return;
    }
    StorePartState* part_state = nullptr;
    u32* generation = nullptr;
    if (event.part == StorePart::STA) {
        part_state = &inst->sta_state;
        generation = &inst->sta_generation;
    } else if (event.part == StorePart::STD) {
        part_state = &inst->std_state;
        generation = &inst->std_generation;
    }
    if (!part_state || !generation || event.generation != *generation) {
        return;
    }
    if (event.result == FuAcceptResult::REPLAY) {
        ++(*generation);
        *part_state = StorePartState::WAIT_ISSUE;
        inst->state = InstState::REPLAY;
        inst->next_valid = false;
        Operand& src = event.part == StorePart::STA
            ? inst->rs1
            : inst->rs2;
        src.bypass_hint = false;
        src.bypass_ready = false;
        src.data_available = false;
        src.speculative_child_mask = 0;
        m_lsu.replay_store_part(inst, event.part);
        requeue_store(inst);
        return;
    }
    if (*part_state != StorePartState::WAIT_ACCEPT) {
        return;
    }
    *part_state = StorePartState::ACCEPTED;
    if (inst->sta_state == StorePartState::ACCEPTED &&
        inst->std_state == StorePartState::ACCEPTED) {
        inst->state = InstState::FU_ACTIVE;
    }
}

void IssueUnit::handle_fu_feedback()
{
    std::vector<FuAcceptEvent> events;
    FuAcceptEvent event {};
    while (m_fu_accept_q.read(event)) {
        if (event.valid) {
            events.emplace_back(event);
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const FuAcceptEvent& lhs,
                        const FuAcceptEvent& rhs) {
                         return lhs.result == FuAcceptResult::REPLAY &&
                                rhs.result != FuAcceptResult::REPLAY;
                     });
    for (const auto& feedback : events) {
        handle_store_feedback(feedback);
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
        } else if (inst->type == InstType::TENSOR) {
            m_tensor_q.emplace_back(inst);
        } else if (inst->type == InstType::LOAD ||
            inst->type == InstType::STA ||
            inst->type == InstType::STD ||
            inst->type == InstType::STORE) {
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
    auto clear_selected = [&]() {
        m_select_buffer.erase(
            std::remove_if(m_select_buffer.begin(), m_select_buffer.end(),
                           [&](const IssueRequest& request) {
                               return request.inst &&
                                      m_rob.is_older_than(
                                          m_flush_event.rid,
                                          request.inst->rid);
                           }),
            m_select_buffer.end());
    };

    switch (m_flush_event.type) {
    case FlushType::BRANCH_MISPRED: {
        clear_q(m_int_q);
        clear_q(m_ls_q);
        clear_selected();
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
        m_fu_accept_q.flush_if([&](const FuAcceptEvent& event) {
            return event.inst &&
                   m_rob.is_older_than(m_flush_event.rid, event.inst->rid);
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
        m_fu_accept_q.clear();
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

void IssueUnit::EmitSrcRead(
    const MInstPtr inst,
    const Operand& src,
    int idx,
    FuType fu)
{
    if (!src.valid || src.bypass_hint) {
        return;
    }
    if (fu == FuType::NONE) {
        fu = GetFuType(inst);
    }
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
    m_fu_accept_q.clear();
}
