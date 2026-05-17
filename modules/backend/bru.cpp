#include "bru.h"
#include "isa/opcode.h"
#include "inst_exec.h"

BRU::BRU(const BRUConfig& conf, const BRUDeps& deps)
    : DevIntf("bru"),
      m_stages(conf.stages),
      m_reg_req_q(*deps.reg_req_q),
      m_reg_resp_q(*deps.reg_resp_q),
      m_reg_bypass_q(*deps.reg_bypass_q),
      m_flush_req_q(*deps.flush_req_q)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
}

bool branch_taken(InstOp op, u64 v1, u64 v2)
{
    switch (op) {
    case OP_BEQ:  return v1 == v2;
    case OP_BNE:  return v1 != v2;
    case OP_BLT:  return static_cast<i64>(v1) < static_cast<i64>(v2);
    case OP_BGE:  return static_cast<i64>(v1) >= static_cast<i64>(v2);
    case OP_BLTU: return v1 < v2;
    case OP_BGEU: return v1 >= v2;
    case OP_JAL: return true;
    case OP_JALR: return true;
    default:
        break;
    }
    return false;
}

void BRU::tick()
{
    E0();
    R1();
    R0();
    dump_states();
    shift();
}

void BRU::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

bool BRU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst)
        return false;
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

bool BRU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

void BRU::R0()
{
    if (!m_pipe[0].valid)
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

void BRU::R1()
{
    if (!m_pipe[1].valid)
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

    m_pred_taken= branch_taken(inst->opcode, inst->rs1.val, inst->rs2.val);
    if (m_pred_taken != inst->pred_taken) {
        if (!m_pred_taken) {
            FlushEvent event;
            event.valid = true;
            event.type = FlushType::BRANCH_MISPRED;
            event.source = FlushSource::BRU;
            event.scope = FlushScope::YOUNGER_THAN;
            event.rid = inst->rid;
            event.redirect = true;
            event.redirect_pc = inst->pc + 4;
            event.branch_taken = false;
            event.recover_c_id = true;
            event.c_id = inst->c_id;
            event.inst_pc = inst->pc;
            event.inst_id = inst->fetch_id;
            event.epoch = inst->epoch;
            m_flush_req_q.write(event);
        }
    }
}

void BRU::E0()
{
    if (!m_pipe[2].valid || !m_pipe[2].inst)
        return;
    MInstPtr inst = m_pipe[2].inst;
    InstExec::BruExecResult ret = InstExec::exec_bru(inst);
    if (!ret.valid)
        return;

    bool mispredict = ret.taken != inst->pred_taken;

    if (mispredict) {
        FlushEvent event;
        event.valid = true;
        event.rid = inst->rid;
        event.type = FlushType::BRANCH_MISPRED;
        event.source = FlushSource::BRU;
        event.scope = FlushScope::YOUNGER_THAN;
        event.redirect = true;
        event.redirect_pc = ret.target;
        event.branch_taken = ret.taken;
        event.recover_c_id = true;
        event.c_id = inst->c_id;
        event.inst_pc = inst->pc;
        event.inst_id = inst->fetch_id;
        event.epoch = inst->epoch;
        m_flush_req_q.write(event);
    }

    if (inst->rd.valid) {
        inst->rd.val = ret.link;
        RegReq req;
        req.valid = true;
        req.write = true;
        req.ptag = inst->rd.ptag;
        req.atag = inst->rd.atag;
        req.fu = FuType::BRU;
        req.val = inst->rd.val;
        req.rid = inst->rid;
        m_reg_req_q.write(req);
    }
}

void BRU::wakeup(MInstPtr inst)
{

}

void BRU::send_redirect(pc_t target)
{

}

void BRU::send_flush(MInstPtr inst)
{
    if (!inst)
        return;
    FlushEvent event;
    event.valid = true;
    event.rid = inst->rid;
    event.type = FlushType::BRANCH_MISPRED;
    m_flush_req_q.write(event);
}

void BRU::flush(const FlushEvent& event)
{

}

void BRU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        m_pipe[i] = m_pipe[i - 1];
    }
    m_pipe[0].valid = false;
    m_pipe[0].inst = nullptr;
}

void BRU::dump_states() const
{
    if (m_pipe[0].valid) {
        auto &inst = m_pipe[0].inst;
        LOG_LEVEL1("BRU R0", "rs1: v-%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_pipe[1].valid) {
        auto &inst = m_pipe[1].inst;
        LOG_LEVEL1("BRU R1", "rs1: v-%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx, pred_taken: %d, real_taken: %d",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val,
            inst->pred_taken, m_pred_taken);
    }
    if (m_pipe[2].valid) {
        auto &inst = m_pipe[2].inst;
        LOG_LEVEL1("BRU E0", "rd: v-%d-a%d-p%d-0x%lx",
            inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val);
    }
}
