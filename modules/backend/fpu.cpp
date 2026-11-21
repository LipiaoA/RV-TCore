#include <cassert>

#include "fpu.h"

FPU::FPU(const FPUConfig& conf, const FPUDeps& deps)
    : DevIntf("fpu")
    , m_stages(conf.stages)
    , m_rob(*deps.rob)
    , m_wakeup_bus(*deps.wakeup_bus)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
    , m_stats(*deps.perf_stats)
{
    assert(m_stages >= 2 && m_stages <= static_cast<i32>(MAX_STAGES));
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

bool FPU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool FPU::dispatch(const MInstPtr inst)
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

void FPU::tick()
{
    if (m_stats.perf()) {
        bool busy = m_input_latch.valid || m_commit_latch.valid;
        for (i32 i = 0; i < m_stages; ++i) {
            busy = busy || m_pipe[i].valid;
        }
        if (busy) {
            ++m_stats.fu[static_cast<u32>(m_fu_type)][m_idx].busy_cycles;
        }
    }
    E0();
    R1();
    shift();
}

void FPU::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0] = m_input_latch;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

void FPU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    auto read_reg = [&](Operand& src) -> bool {
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
    if (inst->rd.valid) {
        wakeup(inst);
    }
}

void FPU::E0()
{
    FUStage& stage = m_pipe[m_stages - 1];
    if (!stage.valid || !stage.inst || m_commit_latch.valid) {
        return;
    }
    MInstPtr inst = stage.inst;
    InstExec::FpuExecResult ret = InstExec::exec_fpu(inst);
    if (ret.valid && inst->rd.valid) {
        inst->rd.val = ret.value;
    }
    stage.valid = false;
    stage.inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
}

void FPU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void FPU::wakeup(MInstPtr inst)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
    WakeupEvent event;
    event.valid = true;
    event.type = WAKEUP_BYPASS;
    event.ptag = inst->rd.ptag;
    event.rid = inst->rid;
    event.rtyp = RegType::GPR;
    m_wakeup_bus.push_back(event);
}

bool FPU::commit_ready() const
{
    return m_commit_latch.valid && m_commit_latch.inst;
}

MInstPtr FPU::commit_inst() const
{
    return m_commit_latch.inst;
}

void FPU::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        ++m_stats.fu[ftyp][m_idx].completed_uops;
    }
}

void FPU::flush(const FlushEvent& event)
{
    auto should_flush = [&](MInstPtr inst) {
        return inst && (event.scope == FlushScope::ALL ||
                        m_rob.is_older_than(event.rid, inst->rid));
    };
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && should_flush(m_pipe[i].inst)) {
            m_pipe[i].inst->valid = false;
            m_pipe[i].inst->state = InstState::FLUSHED;
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
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
}

void FPU::reset()
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
