#include "bru.h"
#include "isa/opcode.h"
#include "isa/decode.h"
#include "inst_exec.h"
#include "rob/rob.h"
#include "branch_predictor.h"

u32 BRU::bru_idx = 0;

BRU::BRU(const BRUConfig& conf, const BRUDeps& deps)
    : DevIntf("bru"),
      m_rob(*deps.rob),
      m_stages(conf.stages),
      m_flush_req_q(*deps.flush_req_q),
      m_wakeup_bus(*deps.wakeup_bus),
      m_reg_resp_q(*deps.reg_resp_q),
      m_reg_bypass_bus(*deps.reg_bypass_bus),
      m_bpu(deps.bpu),
      m_stats(*deps.perf_stats)

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

void BRU::fwd_bypass()
{
    if (!m_pipe[0].valid) {
        return;
    }
    Operand* rs1 = &m_pipe[0].inst->rs1;
    Operand* rs2 = &m_pipe[0].inst->rs2;
    for (Operand* src : {rs1, rs2}) {
        if (!src->valid || !src->bypass_hint) {
            continue;
        }
        for (const auto& resp : m_reg_bypass_bus) {
            if (resp.valid && resp.ptag == src->ptag) {
                src->val = resp.val;
                src->bypass_ready = true;
                break;
            }
        }
    }
    if ((rs1->valid && !rs1->data_available && !rs1->bypass_hint) ||
        (rs2->valid && !rs2->data_available && !rs2->bypass_hint)) {
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
    }
}

static BrType classify_branch(const MicroOp* inst)
{
    if (!inst) {
        return BrType::NONE;
    }
    if (isa::cond_jump(inst->opcode)) {
        return BrType::COND;
    }
    const u32 rd = inst->rd.valid ? static_cast<u32>(inst->rd.atag) : 0;
    const u32 rs1 = static_cast<u32>(inst->rs1.atag);
    if (inst->opcode == OP_JAL) {
        return (rd == 1 || rd == 5) ? BrType::CALL : BrType::JAL;
    }
    if (inst->opcode == OP_JALR) {
        if (rd == 0 && (rs1 == 1 || rs1 == 5)) {
            return BrType::RET;
        }
        if (rd == 1 || rd == 5) {
            return BrType::CALL;
        }
        return BrType::JALR;
    }
    return BrType::NONE;
}

static void train_branch(BranchPredictor* bpu, const MicroOp* inst, bool taken, pc_t target)
{
    if (!bpu || !inst) {
        return;
    }
    BranchUpdate info;
    info.pc = inst->pc;
    info.taken = taken;
    info.target_valid = taken;
    info.target = taken ? target : 0;
    info.type = classify_branch(inst);
    info.ghr_snapshot = inst->bpred.ghr_snapshot;
    info.ras_sp = inst->bpred.ras_sp;
    info.tage_valid = inst->bpred.tage_valid;
    info.tage_final = inst->bpred.tage_final;
    info.tage_provider = inst->bpred.tage_provider;
    info.tage_alt_provider = inst->bpred.tage_alt_provider;
    info.tage_alt_pred = inst->bpred.tage_alt_pred;
    bpu->update(info);
}

static void fill_mispred(FlushEvent& event, const MicroOp* inst, bool taken, pc_t redirect_pc)
{
    event.valid = true;
    event.type = FlushType::BRANCH_MISPRED;
    event.source = FlushSource::BRU;
    event.scope = FlushScope::YOUNGER_THAN;
    event.rid = inst->rid;
    event.redirect = true;
    event.redirect_pc = redirect_pc;
    event.branch_taken = taken;
    event.branch_target = redirect_pc;
    event.recover_c_id = true;
    event.c_id = inst->c_id;
    event.inst_pc = inst->pc;
    event.fetch_id = inst->fetch_id;
    event.epoch = inst->epoch;
    event.bpred = inst->bpred;
    event.bpred.type = classify_branch(inst);
    event.bpred_repair = inst->bpred.valid;
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
    case OP_ECALL: return false;
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
}

void BRU::commit()
{
    shift();
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

bool BRU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst) {
        if (m_stats.perf()) {
            u32 ftyp = static_cast<size_t>(m_fu_type);
            ++m_stats.fu[ftyp][m_idx].rejected_uops;
        }
        return false;
    }
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        ++m_stats.fu[ftyp][m_idx].accepted_uops;
    }
    return true;
}

bool BRU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

void BRU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    Operand *rs1 = &inst->rs1;
    Operand *rs2 = &inst->rs2;
    for (Operand* src : {rs1, rs2}) {
        if (!src->valid || src->bypass_hint) {
            continue;
        }
        for (const auto& resp : m_reg_resp_q) {
            if (resp.valid && resp.ptag == src->ptag) {
                src->val = resp.val;
                src->bypass_ready = true;
                src->data_available = true;
                break;
            }
        }
    }

    m_pred_taken= branch_taken(inst->opcode, inst->rs1.val, inst->rs2.val);

    if (rs1->valid && !rs1->data_available && !rs1->bypass_hint ||
        rs2->valid && !rs2->data_available && !rs2->bypass_hint) {
        return;
    }
    if (inst->rd.valid) {
        wakeup(inst);
    }
    // 预测 taken、实际 not-taken：提前一拍冲刷。训练留在 E0，避免计数器更新两次。
    if (m_pred_taken != inst->pred_taken && !m_pred_taken) {
        FlushEvent event;
        fill_mispred(event, inst, false, inst->pc + 4);
        m_flush_req_q.write(event);
    }
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        if (m_pred_taken != inst->pred_taken) {
            ++m_stats.fu[ftyp][m_idx].fail_direction;
        } else {
            ++m_stats.fu[ftyp][m_idx].succ_direction;
        }
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

    const bool dir_mis = ret.taken != inst->pred_taken;
    const bool tgt_mis = ret.taken && inst->bpred.target_valid && ret.target != inst->bpred.target;
    const pc_t redirect_pc = ret.taken ? ret.target : inst->pc + 4;
    train_branch(m_bpu, inst, ret.taken, ret.target);

    // not-taken 的冲刷已由 R1 发出；这里补上 taken 方向和目标不符。
    if (tgt_mis || (dir_mis && ret.taken)) {
        FlushEvent event;
        fill_mispred(event, inst, ret.taken, redirect_pc);
        m_flush_req_q.write(event);
    }

    if (inst->rd.valid) {
        inst->rd.val = ret.link;
    }
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        if (tgt_mis) {
            ++m_stats.fu[ftyp][m_idx].fail_target;
        } else {
            ++m_stats.fu[ftyp][m_idx].succ_target;
        }
    }
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
    event.rtyp = RegType::GPR;
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
    auto should_flush = [&](MInstPtr inst) {
        return inst && (event.scope == FlushScope::ALL ||
                        m_rob.is_older_than(event.rid, inst->rid));
    };
    for (i32 i = 0; i < m_stages; ++i) {
        if (!m_pipe[i].valid) {
            continue;
        }
        if (should_flush(m_pipe[i].inst)) {
            m_pipe[i].valid = false;
            m_pipe[i].inst->valid = false;
            m_pipe[i].inst->state = InstState::FLUSHED;
            m_pipe[i].inst  = nullptr;
        }
    }
    if (m_commit_latch.valid && should_flush(m_commit_latch.inst)) {
        m_commit_latch.valid = false;
        m_commit_latch.inst->state = InstState::FLUSHED;
        m_commit_latch.inst->valid = false;
        m_commit_latch.inst = nullptr;
    }
    if (m_input_latch.valid && should_flush(m_input_latch.inst)) {
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
        const char *str_rid = inst->rid.str();
        LOG_LEVEL1(log_prefix + "R1", "pc: 0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx, rid:%s",
            inst->pc, inst->inst_name.c_str(), inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val, str_rid);
    }
    if (m_pipe[1].valid) {
        auto inst = m_pipe[1].inst;
        const char *str_rid = inst->rid.str();
        LOG_LEVEL1(log_prefix + "E0", "pc: 0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx,"
            " pred_taken: %d, real_taken: %d, rid:%s",
            inst->pc, inst->inst_name.c_str(), inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val,
            inst->pred_taken, m_pred_taken, str_rid);
    }
    if (m_commit_latch.valid) {
        auto inst = m_commit_latch.inst;
        const char *str_rid = inst->rid.str();
        LOG_LEVEL1(log_prefix + "COMMIT", "pc: 0x%lx, inst: %s, rd: v%d-a%d-p%d-0x%lx, rid:%s",
            inst->pc, inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val, str_rid);
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
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        ++m_stats.fu[ftyp][m_idx].completed_uops;
    }
}

void BRU::reset() {
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}
