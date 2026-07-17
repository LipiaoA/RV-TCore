#include "execute/vector/vector_unit.h"

#include <algorithm>

VectorUnit::VectorUnit(const VectorConfig& conf, const VectorDeps& deps)
    : DevIntf("vector")
    , m_stages(conf.stages)
    , m_rob(*deps.rob)
    , m_wakeup_bus(*deps.wakeup_bus)
    , m_vpu_resp_q(*deps.vpu_resp_q)
{
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_commit_latch.valid = false;
}

bool VectorUnit::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool VectorUnit::dispatch(const MInstPtr inst)
{
    if (!accept() || !inst) {
        return false;
    }
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

void VectorUnit::tick()
{
    E0();
    R1();
    shift();
}

void VectorUnit::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0] = m_input_latch;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

void VectorUnit::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    auto read_vec = [&](Operand& src, VecReg& out) -> bool {
        for (const auto& resp : m_vpu_resp_q) {
            if (resp.valid && resp.ptag == src.ptag) {
                out = resp.val;
                return true;
            }
        }
        return false;
    };

    VecReg vs1_reg;
    VecReg vs2_reg;
    if (inst->vs1.valid && !read_vec(inst->vs1, vs1_reg)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
        return;
    }
    if (inst->vs2.valid && !read_vec(inst->vs2, vs2_reg)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
        return;
    }
    inst->fu_pending = false;
    inst->state = InstState::FU_ACTIVE;
    if (inst->vd.valid) {
        wakeup(inst);
    }
}

void VectorUnit::exec_vector(MInstPtr inst)
{
    VecReg result;
    result.reset(VLEN_BYTES);
    result.vlen = VLEN_BYTES;

    auto read_vec = [&](const Operand& src) -> VecReg {
        for (const auto& resp : m_vpu_resp_q) {
            if (resp.valid && resp.ptag == src.ptag) {
                return resp.val;
            }
        }
        return VecReg {};
    };

    if (inst->vs1.valid && inst->vs2.valid) {
        const VecReg lhs = read_vec(inst->vs1);
        const VecReg rhs = read_vec(inst->vs2);
        const u32 nbytes = std::min({result.vlen, lhs.vlen, rhs.vlen,
                                     static_cast<u32>(lhs.size()),
                                     static_cast<u32>(rhs.size())});
        for (u32 i = 0; i < nbytes; ++i) {
            result.data[i] = static_cast<u8>(lhs.data[i] + rhs.data[i]);
        }
        result.valid = true;
    } else if (inst->rs1.valid) {
        const u64 scalar = inst->rs1.val;
        for (u32 i = 0; i < result.size(); ++i) {
            result.data[i] = static_cast<u8>((scalar >> ((i % 8) * 8)) & 0xff);
        }
        result.valid = true;
    }

    inst->vd_payload = result;
}

void VectorUnit::E0()
{
    FUStage& stage = m_pipe[m_stages - 1];
    if (!stage.valid || !stage.inst || m_commit_latch.valid) {
        return;
    }
    MInstPtr inst = stage.inst;
    exec_vector(inst);
    stage.valid = false;
    stage.inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
}

void VectorUnit::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void VectorUnit::wakeup(MInstPtr inst)
{
    if (!inst || !inst->vd.valid) {
        return;
    }
    WakeupEvent event;
    event.valid = true;
    event.type = WAKEUP_BYPASS;
    event.ptag = inst->vd.ptag;
    event.rid = inst->rid;
    event.rtyp = RegType::VEC;
    m_wakeup_bus.push_back(event);
}

bool VectorUnit::commit_ready() const
{
    return m_commit_latch.valid;
}

MInstPtr VectorUnit::commit_inst() const
{
    return m_commit_latch.inst;
}

void VectorUnit::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void VectorUnit::flush(const FlushEvent& event)
{
    auto should_flush = [&](const MInstPtr& inst) -> bool {
        if (!inst) {
            return true;
        }
        if (event.scope == FlushScope::ALL) {
            return true;
        }
        return m_rob.is_older_than(event.rid, inst->rid);
    };

    if (m_input_latch.valid && should_flush(m_input_latch.inst)) {
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
    if (m_commit_latch.valid && should_flush(m_commit_latch.inst)) {
        m_commit_latch.valid = false;
        m_commit_latch.inst = nullptr;
    }
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && should_flush(m_pipe[i].inst)) {
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
}

void VectorUnit::reset()
{
    m_input_latch.valid = false;
    m_commit_latch.valid = false;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}
