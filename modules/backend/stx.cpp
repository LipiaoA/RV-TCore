#include <cassert>

#include "stx.h"
#include "lsu.h"

u32 STD::std_idx = 0;

STD::STD(const STDConfig& conf, const STDDeps& deps)
    : DevIntf("std")
    , m_stages(conf.stages)
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
    m_idx = STD::std_idx++;
}

void STD::fwd_bypass()
{
    if (!m_pipe[0].valid || !m_pipe[0].request.inst) {
        return;
    }
    IssueRequest& request = m_pipe[0].request;
    if (request.part == StorePart::STD &&
        m_pipe[0].feedback_sent) {
        return;
    }
    MInstPtr inst = request.inst;
    Operand& src = request.part == StorePart::STD
        ? inst->rs2
        : inst->rs1;
    if (!src.valid || !src.bypass_hint) {
        return;
    }
    bool captured = src.data_available || capture_from_bypass(src);
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
}

void STD::tick()
{
    E0();
    R1();
    dump_states();
}

bool STD::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool STD::dispatch(const IssueRequest& request)
{
    if (!accept() || !request.inst) {
        return false;
    }
    m_input_latch.request = request;
    m_input_latch.feedback_sent = false;
    m_input_latch.valid = true;
    return true;
}

bool STD::dispatch(MInstPtr inst)
{
    StorePart part = inst && inst->type == InstType::STD
        ? StorePart::STD
        : StorePart::NONE;
    return dispatch({inst, part, inst ? inst->std_generation : 0});
}

bool STD::capture_from_bypass(Operand& src)
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

bool STD::capture_from_regfile(Operand& src)
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

bool STD::capture_operand(IssueStage& stage)
{
    Operand& src = stage.request.part == StorePart::STD
        ? stage.request.inst->rs2
        : stage.request.inst->rs1;
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

bool STD::send_feedback(
    const IssueRequest& request,
    FuAcceptResult result)
{
    if (request.part != StorePart::STD) {
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

void STD::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].request.inst) {
        return;
    }
    IssueRequest& request = m_pipe[0].request;
    if (request.part == StorePart::STD &&
        m_pipe[0].feedback_sent) {
        return;
    }
    MInstPtr inst = request.inst;
    Operand& src = request.part == StorePart::STD
        ? inst->rs2
        : inst->rs1;
    if (src.bypass_hint) {
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
}

void STD::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].request.inst) {
        return;
    }
    const IssueRequest request = m_pipe[1].request;
    MInstPtr inst = request.inst;
    if (request.part == StorePart::STD &&
        request.generation != inst->std_generation) {
        m_pipe[1].valid = false;
        m_pipe[1].request = {};
        return;
    }

    SQEntry* sq = m_lsu.find_sq_for_std(inst);
    if (!sq) {
        return;
    }
    sq->data = inst->rs2.valid ? inst->rs2.val : inst->data;
    sq->data_ready = true;
    if (sq->ls_width == 0) {
        sq->ls_width = inst->ls_width;
    }
    m_lsu.on_store_operands_updated(*sq);


    LOG_LEVEL1("STD E0", "store pc:0x%lx id:%lu stq idx:%zu addr:0x%lx data:0x%lx width:%u rid:%s%u",
               inst->pc, sq->store_id, sq->stq_idx, sq->addr, sq->data, sq->ls_width,
               inst->rid.direct ? "--" : "++", inst->rid.idx);
    m_pipe[1].valid = false;
    m_pipe[1].request = {};
}

void STD::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].request = {};
        }
    }
}

void STD::flush(const FlushEvent& event)
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

void STD::commit()
{
    shift();
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].request = m_input_latch.request;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.request = {};
    }
}


void STD::reset()
{
    m_input_latch.valid = false;
    m_input_latch.request = {};
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].request = {};
    }
}

void STD::dump_states() const
{

}
