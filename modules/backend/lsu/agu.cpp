#include <cassert>

#include "agu.h"
#include "lsu.h"
#include "inst_exec.h"

u32 AGU::agu_idx = 0;

AGU::AGU(const AGUConfig& conf, const AGUDeps& deps)
    : DevIntf("agu")
    , m_stages(conf.stages)
    , lsu_ld_q(*deps.lsu_ld_q)
    , lsu_st_q(*deps.lsu_st_q)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
    , m_fu_accept_q(*deps.fu_accept_q)
    , m_rob(*deps.rob)
    , m_lsu(*deps.lsu)
{
    assert(m_stages >= 2 && m_stages <= static_cast<i32>(MAX_STAGES));
    for (i32 i = 0; i < static_cast<i32>(MAX_STAGES); i++) {
        m_pipe[i].valid = false;
        m_pipe[i].request = {};
    }
    m_input_latch.valid = false;
    m_input_latch.request = {};
    m_idx = AGU::agu_idx++;
}

void AGU::fwd_bypass()
{
    if (!m_pipe[0].valid || !m_pipe[0].request.inst) {
        return;
    }
    IssueRequest& request = m_pipe[0].request;
    if (request.part == StorePart::STA) {
        if (m_pipe[0].feedback_sent) {
            return;
        }
        Operand& src = request.inst->rs1;
        if (!src.valid || !src.bypass_hint) {
            return;
        }
        FuAcceptResult result =
            (src.data_available || capture_from_bypass(src))
            ? FuAcceptResult::ACCEPTED
            : FuAcceptResult::REPLAY;
        if (!send_feedback(request, result)) {
            return;
        }
        m_pipe[0].feedback_sent = true;
        if (result == FuAcceptResult::REPLAY) {
            m_pipe[0].valid = false;
            m_pipe[0].request = {};
        }
        return;
    }
    MInstPtr inst = request.inst;
    Operand* rs1 = &inst->rs1;
    Operand* rs2 = &inst->rs2;
    for (Operand* src : {rs1, rs2}) {
        if (!src->valid || !src->bypass_hint) {
            continue;
        }
        for (const auto& resp : m_reg_bypass_bus) {
            if (resp.valid && resp.ptag == src->ptag) {
                src->val = resp.val;
                src->bypass_ready = true;
                src->data_available = true;
                break;
            }
        }
    }
    if ((rs1->valid && !rs1->data_available) || (rs2->valid && !rs2->data_available)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].request = {};
    } else {
        inst->fu_pending = false;
        inst->state = InstState::FU_ACTIVE;
    }
}

void AGU::tick()
{
    E0();
    R1();
    dump_states();
}

bool AGU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool AGU::dispatch(const IssueRequest& request)
{
    if (!accept() || !request.inst) {
        return false;
    }
    m_input_latch.request = request;
    m_input_latch.feedback_sent = false;
    m_input_latch.valid = true;
    return true;
}

bool AGU::dispatch(MInstPtr inst)
{
    StorePart part = inst && inst->type == InstType::STA
        ? StorePart::STA
        : StorePart::NONE;
    return dispatch({inst, part, inst ? inst->sta_generation : 0});
}

bool AGU::capture_from_bypass(Operand& src)
{
    for (const auto& resp : m_reg_bypass_bus) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            src.bypass_ready = true;
            src.data_available = true;
            return true;
        }
    }
    return false;
}

bool AGU::capture_from_regfile(Operand& src)
{
    for (const auto& resp : m_reg_resp_q) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            src.data_available = true;
            return true;
        }
    }
    return false;
}

bool AGU::capture_operand(IssueStage& stage)
{
    Operand& src = stage.request.inst->rs1;
    if (!src.valid) {
        return true;
    }
    if (src.data_available) {
        return true;
    }
    if (src.bypass_hint) {
        return capture_from_bypass(src);
    }
    return capture_from_regfile(src);
}

bool AGU::send_feedback(
    const IssueRequest& request,
    FuAcceptResult result)
{
    if (request.part != StorePart::STA) {
        return true;
    }
    FuAcceptEvent event {};
    event.valid = true;
    event.inst = request.inst;
    event.part = request.part;
    event.result = result;
    event.generation = request.generation;
    return m_fu_accept_q.write(event);
}

void AGU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].request.inst) {
        return;
    }
    IssueRequest& request = m_pipe[0].request;
    MInstPtr inst = request.inst;
    if (request.part == StorePart::STA) {
        if (m_pipe[0].feedback_sent) {
            return;
        }
        if (inst->rs1.valid && inst->rs1.bypass_hint) {
            return;
        }
        bool captured = capture_operand(m_pipe[0]);
        if (!send_feedback(request, captured
                ? FuAcceptResult::ACCEPTED
                : FuAcceptResult::REPLAY)) {
            return;
        }
        m_pipe[0].feedback_sent = true;
        if (!captured) {
            m_pipe[0].valid = false;
            m_pipe[0].request = {};
        }
        return;
    }
    if (!inst->rs1.valid) {
        inst->fu_pending = false;
        inst->state = InstState::FU_ACTIVE;
        return;
    }
    auto& src = inst->rs1;
    if (src.bypass_hint) {
        return;
    }
    // read from reg prf
    for (const auto& resp : m_reg_resp_q) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            src.data_available = true;
            break;
        }
    }
    if ((src.valid && !src.data_available)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].request = {};
    } else {
        inst->fu_pending = false;
        inst->state = InstState::FU_ACTIVE;
    }
}

void AGU::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].request.inst) {
        return;
    }
    const IssueRequest request = m_pipe[1].request;
    MInstPtr inst = request.inst;
    if (request.part == StorePart::STA &&
        request.generation != inst->sta_generation) {
        m_pipe[1].valid = false;
        m_pipe[1].request = {};
        return;
    }

    InstExec::AguExecResult ret = InstExec::exec_agu(inst);
    if (!ret.valid) {
        return;
    }

    if (request.part == StorePart::STA ||
        inst->type == InstType::STA) {
        SQEntry* st = m_lsu.find_store_by_id(inst->store_id);
        if (!st) {
            return;
        }
        auto& st_entry = *st;
        st_entry.addr = ret.addr;
        st_entry.addr_ready = true;
        if (st_entry.ls_width == 0) {
            st_entry.ls_width = inst->ls_width;
        }
        m_lsu.on_store_operands_updated(st_entry);
        LOG_LEVEL1("AGU E0", "addr ready,store pc:0x%lx id:%lu addr:0x%lx rid:%s%u",
                   inst->pc, st_entry.store_id, st_entry.addr, inst->rid.direct ? "--" : "++", inst->rid.idx);
    } else if (inst->type == InstType::LOAD) {
        LQEntry* ld = m_lsu.find_load_by_id(inst->load_idx);
        if (!ld) {
            return;
        }
        ld->addr = ret.addr;
        ld->addr_ready = true;
        LOG_LEVEL1("AGU E0", "addr ready, load pc:0x%lx id:%lu addr:0x%lx rid:%s%u",
                   inst->pc, ld->load_id, ld->addr, inst->rid.direct ? "--" : "++", inst->rid.idx);
    }
    m_pipe[1].valid = false;
    m_pipe[1].request = {};
}

void AGU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].request = {};
        }
    }
}

void AGU::flush(const FlushEvent& event)
{
    auto should_flush = [&](MInstPtr inst) {
        return inst && (event.scope == FlushScope::ALL ||
                        m_rob.is_older_than(event.rid, inst->rid));
    };
    if (m_input_latch.valid &&
        should_flush(m_input_latch.request.inst)) {
        m_input_latch.request.inst->valid = false;
        m_input_latch.request.inst->state = InstState::FLUSHED;
        m_input_latch.valid = false;
        m_input_latch.request = {};
    }
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid &&
            should_flush(m_pipe[i].request.inst)) {
            m_pipe[i].request.inst->valid = false;
            m_pipe[i].request.inst->state = InstState::FLUSHED;
            m_pipe[i].valid = false;
            m_pipe[i].request = {};
        }
    }
}

void AGU::commit()
{
    shift();
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].request = m_input_latch.request;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.request = {};
    }
}

void AGU::reset() {
    m_input_latch.valid = false;
    m_input_latch.request = {};
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].request = {};
    }
}

void AGU::dump_states() const
{

}



