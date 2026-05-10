#include "bru.h"

#include "isa/opcode.h"

bool branch_taken(InstOp op, u64 rs1, u64 rs2)
{
    switch (op) {
    case OP_BEQ:  return rs1 == rs2;
    case OP_BNE:  return rs1 != rs2;
    case OP_BLT:  return static_cast<i64>(rs1) < static_cast<i64>(rs2);
    case OP_BGE:  return static_cast<i64>(rs1) >= static_cast<i64>(rs2);
    case OP_BLTU: return rs1 < rs2;
    case OP_BGEU: return rs1 >= rs2;
    default:      return false;
    }
}

BRU::BRU(const BRUConfig& conf)
    : DevIntf("bru")
    , m_stages(conf.stages)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void BRU::tick()
{
    flush();
    E0();
    R1();
    R0();
    shift();
}

void BRU::commit()
{
}

bool BRU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst)
        return false;
    m_pipe[0].inst  = inst;
    m_pipe[0].valid = true;
    return true;
}

bool BRU::accept() const
{
    return !m_pipe[0].valid;
}

void BRU::R0()
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

void BRU::R1()
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
    u64 rs1 = inst->rs1.valid ? inst->rs1.val : 0;
    u64 rs2 = inst->rs2.valid ? inst->rs2.val : 0;
    m_pred_taken= branch_taken(inst->opcode, rs1, rs2);
    if (m_pred_taken != inst->pred_taken) {
        if (!m_pred_taken) {
            send_redirect(inst->pc + 4);
        }
        send_flush(inst);
    }
}

void BRU::E0()
{
    if (!m_pipe[2].valid || !m_pipe[2].inst)
        return;
    MInstPtr inst = m_pipe[2].inst;
    if (exec)
        exec(inst);

    pc_t target = inst->rd.val;
    bool mispredict =
        (m_pred_taken != inst->pred_taken) ||
        (m_pred_taken && target != inst->rd.val);

    if (mispredict) {
        pc_t redirect_pc = inst->rd.val;
        send_redirect(redirect_pc);
        send_flush(inst);
    }
}

void BRU::send_redirect(pc_t target)
{
    (void)target;
}

void BRU::send_flush(MInstPtr inst)
{
    if (!inst)
        return;
    FlushEvent event;
    event.valid = true;
    event.rob_id = inst->rob_id;
    event.type = FlushType::BRANCH;
    m_flush_q.push_back(event);
}

void BRU::flush()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i]            = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
}

void BRU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i]            = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
}
