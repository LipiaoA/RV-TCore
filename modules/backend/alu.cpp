#include "alu.h"
#include "rename/prf.h"
#include "inst_exec.h"

ALU::ALU(const ALUConfig& conf, const ALUDeps& deps)
    : DevIntf("alu")
    , m_stages(conf.stages)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_q(*deps.reg_bypass_q)
    , m_rob(*deps.rob)
{
    assert(m_stages == 3);
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void ALU::tick()
{
    E0();
    R1();
    R0();
    dump_states();
    shift();
}

void ALU::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

bool ALU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool ALU::dispatch(const MInstPtr inst)
{
    if (!accept() || !inst)
        return false;
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

void ALU::R0()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst)
        return;
    RegReq req;
    MInstPtr inst = m_pipe[0].inst;
    if (inst->rs1.valid) {
        req.valid = true;
        req.ptag = inst->rs1.ptag;
        req.write = false;
        m_reg_req_q.write(req);
    }
    if (inst->rs2.valid) {
        req.valid = true;
        req.ptag = inst->rs2.ptag;
        req.write = false;
        m_reg_req_q.write(req);
    }
}

void ALU::R1()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst || m_reg_resp_q.empty())
        return;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid) {
        bool found = false;
        for (auto& resp : m_reg_bypass_q) {
            if (resp.valid && resp.ptag == inst->rs1.ptag) {
                inst->rs1.val = resp.val;
                found = true;
                break;
            }
        }
        if (!found) {
            for (auto& resp : m_reg_resp_q) {
                if (resp.valid && resp.ptag == inst->rs1.ptag) {
                    inst->rs1.val = resp.val;
                    break;
                }
            }
        }
    }

    if (inst->rs2.valid) {
        bool found = false;
        for (auto& resp : m_reg_bypass_q) {
            if (resp.valid && resp.ptag == inst->rs2.ptag) {
                inst->rs2.val = resp.val;
                found = true;
                break;
            }
        }
        if (!found) {
            for (auto& resp : m_reg_resp_q) {
                if (resp.valid && resp.ptag == inst->rs2.ptag) {
                    inst->rs2.val = resp.val;
                    break;
                }
            }
        }
    }
    wakeup(inst);
}

void ALU::E0()
{
    if (!m_pipe[2].valid || !m_pipe[2].inst || m_commit_latch.valid) {
        return;
    }
        return;
    MInstPtr inst = m_pipe[2].inst;
    InstExec::AluExecResult ret = InstExec::exec_alu(inst);
    (void)ret;
    broadcast(inst);
    if (inst->rd.valid) {
        inst->rd.val = ret.value;
    }
    m_pipe[2].valid = false;
    m_pipe[2].inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
    return;
}

void ALU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void ALU::wakeup(MInstPtr inst)
{
    if (!inst || !inst->rd.valid)
        return;
    WakeupEvent event;
    event.valid = true;
    event.ptag   = inst->rd.ptag;
    m_wakeup_q.push_back(event);
}

void ALU::broadcast(MInstPtr inst)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
}

void ALU::flush(const FlushEvent& event)
{
    for (i32 i = 0; i < m_stages; ++i) {
        if (!m_pipe[i].valid) {
            continue;
        }
        if (m_rob.is_older_than(event.rid, m_pipe[i].inst->rid)) {
            m_pipe[i].valid = false;
            m_pipe[i].inst->valid = false;
            m_pipe[i].inst->state = InstState::FLUSHED;
            m_pipe[i].inst  = nullptr;
        }
    }
}

bool ALU::commit_ready() const
{
    return m_commit_latch.valid && m_commit_latch.inst;
}

MInstPtr ALU::commit_inst() const
{
    return m_commit_latch.inst;
}

void ALU::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void ALU::dump_states() const
{
    if (m_pipe[0].valid) {
        auto inst = m_pipe[0].inst;
        LOG_LEVEL1("ALU R0", "rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_pipe[1].valid) {
        auto inst = m_pipe[1].inst;
        LOG_LEVEL1("ALU R1", "rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_commit_latch.valid) {
        auto inst = m_commit_latch.inst;
        LOG_LEVEL1("ALU E0", "rd: v%d-a%d-p%d-0x%lx",
            inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val);
    }
}