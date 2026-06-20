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
    , m_rob(*deps.rob)
    , m_lsu(*deps.lsu)
{
    assert(m_stages >= 2 && m_stages <= static_cast<i32>(MAX_STAGES));
    for (i32 i = 0; i < static_cast<i32>(MAX_STAGES); i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    m_idx = STD::std_idx++;
}

void STD::tick()
{
    E0();
    bool r1_ready = R1();
    dump_states();
    shift(r1_ready);
}

bool STD::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool STD::dispatch(MInstPtr inst)
{
    if (!accept() || !inst) {
        return false;
    }
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

bool STD::read_reg(Operand& src)
{
    for (auto& resp : m_reg_bypass_bus) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            return true;
        }
    }
    for (auto& resp : m_reg_resp_q) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            return true;
        }
    }
    return false;
}

bool STD::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return true;
    }
    MInstPtr inst = m_pipe[0].inst;
    if (inst->rs2.valid && !read_reg(inst->rs2)) {
        return false;
    }
    return true;
}

void STD::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst || m_commit_latch.valid) {
        return;
    }
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs2.valid && !read_reg(inst->rs2)) {
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

    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
}

void STD::shift(bool r1_ready)
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (i == 1 && !r1_ready) {
            continue;
        }
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void STD::flush(const FlushEvent& event)
{
    auto should_flush = [&](MInstPtr inst) {
        return inst && (event.scope == FlushScope::ALL ||
                        m_rob.is_older_than(event.rid, inst->rid));
    };
    if (m_input_latch.valid && should_flush(m_input_latch.inst)) {
        m_input_latch.inst->valid = false;
        m_input_latch.inst->state = InstState::FLUSHED;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
    if (m_commit_latch.valid && should_flush(m_commit_latch.inst)) {
        m_commit_latch.inst->valid = false;
        m_commit_latch.inst->state = InstState::FLUSHED;
        m_commit_latch.valid = false;
        m_commit_latch.inst = nullptr;
    }
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && should_flush(m_pipe[i].inst)) {
            m_pipe[i].inst->valid = false;
            m_pipe[i].inst->state = InstState::FLUSHED;
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
}

void STD::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

bool STD::commit_ready() const
{
    return m_commit_latch.valid && m_commit_latch.inst;
}

MInstPtr STD::commit_inst() const
{
    return m_commit_latch.inst;
}

void STD::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void STD::reset()
{
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void STD::dump_states() const
{
    std::string log_prefix = "STD" + std::to_string(m_idx) + " ";
    if (m_pipe[0].valid) {
        auto inst = m_pipe[0].inst;
        LOG_LEVEL1(log_prefix + "R0", "rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_pipe[1].valid) {
        auto inst = m_pipe[1].inst;
        LOG_LEVEL1(log_prefix + "R1", "rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
}
