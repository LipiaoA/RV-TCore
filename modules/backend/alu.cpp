#include "alu.h"
#include "inst_exec.h"

u32 ALU::alu_idx = 0;

ALU::ALU(const ALUConfig& conf, const ALUDeps& deps)
    : DevIntf("alu")
    , m_stages(conf.stages)
    , m_rob(*deps.rob)
    , m_wakeup_bus(*deps.wakeup_bus)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
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
    m_idx = ALU::alu_idx++;
}

void ALU::fwd_bypass()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
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
                src->data_available = true;
                break;
            }
        }
    }
    if ((rs1->valid && !rs1->data_available) || (rs2->valid && !rs2->data_available)) {
        m_pipe[0].inst->fu_pending = false;
        m_pipe[0].inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
    } else {
        m_pipe[0].inst->fu_pending = false;
        m_pipe[0].inst->state = InstState::FU_ACTIVE;
        WakeupEvent event;
        event.valid = true;
        event.type = WAKEUP_BYPASS;
        event.ptag = rs1->ptag;
        event.rid = m_pipe[0].inst->rid;
        event.rtyp = RegType::GPR;
        event.bypassable = true;
        event.speculative = true;
        event.speculative_mask = 1u << m_idx;
    }
    return;
}

void ALU::tick()
{
    E0();
    R1();
    dump_states();
}

void ALU::commit()
{
    shift();
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
    if (inst->rd.valid) {
        wakeup(inst);
    }
    return true;
}

void ALU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    auto& rs1 = inst->rs1;
    auto& rs2 = inst->rs2;
    for (Operand* src : {&rs1, &rs2}) {
        if (!src->valid || src->bypass_hint) {
            continue;
        }
        for (const auto& resp : m_reg_resp_q) {
            if (resp.valid && resp.ptag == src->ptag) {
                src->val = resp.val;
                src->data_available = true;
                break;
            }
        }
    }
    if ((rs1.valid && rs1.bypass_hint) ||
        (rs2.valid && rs2.bypass_hint)) {
        return;
    }
    if ((rs1.valid && !rs1.data_available) || (rs2.valid && !rs2.data_available)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
    } else {
        inst->fu_pending = false;
        inst->state = InstState::FU_ACTIVE;
    }
}

void ALU::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst || m_commit_latch.valid) {
        // broadcast(m_commit_latch.inst);
        return;
    }
    MInstPtr inst = m_pipe[1].inst;
    InstExec::AluExecResult ret = InstExec::exec_alu(inst);
    (void)ret;
    if (inst->rd.valid) {
        inst->rd.val = ret.value;
        broadcast(inst);
    }
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
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
    event.type  = WAKEUP_BYPASS;
    event.ptag   = inst->rd.ptag;
    event.rid    = inst->rid;
    event.rtyp = RegType::GPR;
    event.bypassable = true;
    event.speculative = true;
    event.speculative_mask = 1u << m_idx;
    event.producer_fu = FuType::ALU;
    event.producer_fu_id = static_cast<int>(m_idx);
    m_wakeup_bus.push_back(event);
}

void ALU::broadcast(MInstPtr inst)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
    RegResp resp {};
    resp.valid = true;
    resp.ptag = inst->rd.ptag;
    resp.val = inst->rd.val;
    resp.rid = inst->rid;
    resp.fu = FuType::ALU;
    resp.fu_id = static_cast<int>(m_idx);
    resp.rtyp = RegType::GPR;
    if (!m_reg_bypass_bus.full()) {
        m_reg_bypass_bus.push_back(resp);
    } else {
        assert(false);
    }
}

void ALU::flush(const FlushEvent& event)
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
        m_input_latch.inst->state = InstState::FLUSHED;
        m_input_latch.inst->valid = false;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
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
    std::string log_prefix = "EXECUTE ALU" + std::to_string(m_idx) + " ";
    auto print_inst = [](std::string log_prefix, const MInstPtr inst) {
        auto &rs1 = inst->rs1;
        auto &rs2 = inst->rs2;
        auto &rd = inst->rd;
        LOG_LEVEL1(log_prefix, "pc:0x%lx, inst: %s,rs1:v%d-x%d-p%d-0x%lx,rs2:v%d-x%d-p%d-0x%lx,"
            "rd:v%d-x%d-p%d-0x%016lx,imm:0x%08lx",
            inst->pc, inst->inst_name.c_str(), rs1.valid, rs1.atag, rs1.ptag, rs1.val,
            rs2.valid, rs2.atag, rs2.ptag, rs2.val, rd.valid, rd.atag, rd.ptag, rd.val,
            inst->imm);
    };
    if (m_pipe[0].valid) {
        print_inst(log_prefix + "R1 ", m_pipe[0].inst);
    }

    if (m_commit_latch.valid) {
        print_inst(log_prefix + "E0 ", m_commit_latch.inst);
    }
}

void ALU::reset() {
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}
