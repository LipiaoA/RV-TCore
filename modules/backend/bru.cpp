#include "bru.h"
#include "isa/opcode.h"
#include "inst_exec.h"
#include "rob/rob.h"

u32 BRU::bru_idx = 0;

BRU::BRU(const BRUConfig& conf, const BRUDeps& deps)
    : DevIntf("bru"),
      m_rob(*deps.rob),
      m_stages(conf.stages),
      m_flush_req_q(*deps.flush_req_q),
      m_wakeup_bus(*deps.wakeup_bus),
      m_reg_resp_q(*deps.reg_resp_q),
      m_reg_bypass_bus(*deps.reg_bypass_bus)
{
    assert(m_stages == 2);
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    m_idx = BRU::bru_idx++;
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

void BRU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst)
        return;
    MInstPtr inst = m_pipe[0].inst;
    auto read_reg = [&](RegType &src) -> bool {
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
    };
    if (inst->rs1.valid && !read_reg(inst->rs1)) {
        return;
    }
    if (inst->rs2.valid && !read_reg(inst->rs2)) {
        return;
    }
    m_pred_taken= branch_taken(inst->opcode, inst->rs1.val, inst->rs2.val);
    if (inst->rd.valid) {
        wakeup(inst);
    }
    /* predict taken but not taken, flush*/
    if (m_pred_taken != inst->pred_taken && !m_pred_taken) {
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
        event.fetch_id = inst->fetch_id;
        event.epoch = inst->epoch;
        m_flush_req_q.write(event);
    }
}

void BRU::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst || m_commit_latch.valid) {
        return;
    }
    MInstPtr inst = m_pipe[1].inst;
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
        event.fetch_id = inst->fetch_id;
        event.epoch = inst->epoch;
        m_flush_req_q.write(event);
    }

    if (inst->rd.valid) {
        inst->rd.val = ret.link;
    }
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
    return;
}

void BRU::wakeup(MInstPtr inst)
{
    if (!inst || !inst->rd.valid)
        return;
    WakeupEvent event;
    event.valid = true;
    event.type  = WAKEUP_BYPASS;
    event.ptag  = inst->rd.ptag;
    event.rid   = inst->rid;
    m_wakeup_bus.push_back(event);
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
    if (m_commit_latch.valid && m_rob.is_older_than(event.rid, m_commit_latch.inst->rid)) {
        m_commit_latch.valid = false;
        m_commit_latch.inst->state = InstState::FLUSHED;
        m_commit_latch.inst->valid = false;
        m_commit_latch.inst = nullptr;
    }
    if (m_input_latch.valid && m_rob.is_older_than(event.rid, m_input_latch.inst->rid)) {
        m_input_latch.valid = false;
        m_input_latch.inst->state = InstState::FLUSHED;
        m_input_latch.inst->valid = false;
        m_input_latch.inst = nullptr;
    }
}

void BRU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void BRU::dump_states() const
{
    std::string log_prefix = "BRU" + std::to_string(m_idx) + " ";
    if (m_pipe[0].valid) {
        auto inst = m_pipe[0].inst;
        LOG_LEVEL1(log_prefix + "R0", "pc: 0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->pc, inst->inst_name.c_str(), inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_pipe[1].valid) {
        auto inst = m_pipe[1].inst;
        LOG_LEVEL1(log_prefix + "R1", "pc: 0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx, pred_taken: %d, real_taken: %d",
            inst->pc, inst->inst_name.c_str(), inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val,
            inst->pred_taken, m_pred_taken);
    }
    if (m_commit_latch.valid) {
        auto inst = m_commit_latch.inst;
        LOG_LEVEL1(log_prefix + "E0", "pc: 0x%lx, inst: %s, rd: v%d-a%d-p%d-0x%lx",
            inst->pc, inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val);
    }
}


bool BRU::commit_ready() const
{
    return m_commit_latch.valid && m_commit_latch.inst;
}

MInstPtr BRU::commit_inst() const
{
    return m_commit_latch.inst;
}

void BRU::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void BRU::reset() {
    return;
}
