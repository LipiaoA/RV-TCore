#include "alu.h"
#include "rename/prf.h"

ALU::ALU(const ALUConfig& conf)
    : DevIntf("alu")
    , m_stages(conf.stages)
    , m_latency(conf.latency)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void ALU::tick()
{
    flush();
    E1();
    E0();
    R1();
    R0();
    shift();
}

void ALU::commit()
{
}

bool ALU::accept() const
{
    return !m_pipe[0].valid;
}

bool ALU::dispatch(const MInstPtr inst)
{
    if (!accept() || !inst)
        return false;
    m_pipe[0].inst  = inst;
    m_pipe[0].valid = true;
    return true;
}

void ALU::R0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst)
        return;
    RegReq req;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid) {
        req.valid = true;
        req.ptag = inst->rs1.ptag;
        req.write = false;
        m_reg_read_q.push_back(req);
    }
    if (inst->rs2.valid) {
        req.valid = true;
        req.ptag = inst->rs2.ptag;
        req.write = false;
        m_reg_read_q.push_back(req);
    }
}

void ALU::R1()
{
    if (!m_pipe[2].valid || !m_pipe[2].inst || m_reg_resp.empty())
        return;
    MInstPtr inst = m_pipe[2].inst;
    if (inst->rs1.valid && m_reg_resp[0].valid) {
        inst->rs1.val = m_reg_resp[0].val;
    }
    if (inst->rs2.valid && m_reg_resp.size() > 1 && m_reg_resp[1].valid) {
        inst->rs2.val = m_reg_resp[1].val;
    }
    wakeup(inst);
}

void ALU::E0()
{
    if (!m_pipe[3].valid || !m_pipe[3].inst)
        return;
    MInstPtr inst = m_pipe[3].inst;
    if (exec)
        exec(inst);
    broadcast(inst);
}

void ALU::E1()
{
    if (!m_pipe[4].valid || !m_pipe[4].inst)
        return;
    MInstPtr inst = m_pipe[4].inst;
    if (inst->rd.valid) {
        RegReq req;
        req.valid = true;
        req.ptag = inst->rd.ptag;
        req.write = true;
        req.val = inst->rd.val;
        m_reg_write_q.push_back(req);
    }
}

void ALU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i]            = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
}

void ALU::wakeup(MInstPtr inst)
{
    if (!inst || !inst->rd.valid)
        return;
    WakeupEvent event;
    event.valid = true;
    event.tag   = inst->rd.ptag;
    m_wakeup_q.push_back(event);
}

void ALU::broadcast(MInstPtr inst)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
}

void ALU::flush()
{
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid) {
            m_pipe[i].valid = false;
            m_pipe[i].inst  = nullptr;
        }
    }
}
