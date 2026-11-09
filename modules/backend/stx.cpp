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
    , m_stats(*deps.perf_stats)
{
    assert(m_stages >= 2 && m_stages <= static_cast<i32>(MAX_STAGES));
    for (i32 i = 0; i < static_cast<i32>(MAX_STAGES); i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_idx = STD::std_idx++;
}

void STD::fwd_bypass()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    // STORE 半边用 rs2 作数据；旧 STD MicroOp 用 rs1
    Operand* data = (inst->type == InstType::STORE) ? &inst->rs2 : &inst->rs1;
    if (!data->valid || !data->bypass_hint) {
        return;
    }
    for (const auto& resp : m_reg_bypass_bus) {
        if (resp.valid && resp.ptag == data->ptag) {
            data->val = resp.val;
            data->bypass_ready = true;
            data->data_available = true;
            break;
        }
    }
    if (data->valid && !data->bypass_ready && data->bypass_hint) {
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
    }
}

void STD::tick()
{
    E0();
    R1();
    dump_states();
}

bool STD::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool STD::dispatch(MInstPtr inst)
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

void STD::R1()
{
    MInstPtr inst = m_pipe[0].inst;
    if (!m_pipe[0].valid || !inst) {
        return;
    }
    Operand& src = inst->rs2;
    if (!src.valid || src.bypass_hint) {
        return;
    }
    for (const auto& resp : m_reg_resp_q) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            src.data_available = true;
            break;
        }
    }
    if (src.valid && !src.data_available && !src.bypass_hint) {
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
    }
}

void STD::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst) {
        return;
    }
    MInstPtr inst = m_pipe[1].inst;

    SQEntry* sq = m_lsu.find_sq_for_std(inst);
    if (!sq) {
        return;
    }
    if (inst->type == InstType::STORE) {
        sq->data = inst->rs2.valid ? inst->rs2.val : inst->data;
    } else {
        sq->data = inst->rs2.valid ? inst->rs2.val
                                   : (inst->rs1.valid ? inst->rs1.val : inst->data);
    }
    sq->data_ready = true;
    if (sq->ls_width == 0) {
        sq->ls_width = inst->ls_width;
    }
    inst->std_done = true;
    inst->data = sq->data;
    inst->data_ready = true;
    m_lsu.on_store_operands_updated(*sq);

    LOG_LEVEL1("DGU E0", "store pc:0x%lx id:%lu stq idx:%zu addr:0x%lx data:0x%lx width:%u rid:%s%u",
               inst->pc, sq->store_id, sq->stq_idx, sq->addr, sq->data, sq->ls_width,
               inst->rid.direct ? "--" : "++", inst->rid.idx);
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
    if (m_stats.perf()) {
        u32 ftyp = static_cast<size_t>(m_fu_type);
        ++m_stats.fu[ftyp][m_idx].completed_uops;
    }
}

void STD::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
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
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && should_flush(m_pipe[i].inst)) {
            m_pipe[i].inst->valid = false;
            m_pipe[i].inst->state = InstState::FLUSHED;
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
}

void STD::squash_rid(const ROBID& rid, i32 ptag)
{
    auto match = [&](MInstPtr inst) {
        return inst &&
        inst->rid.idx == rid.idx &&
        inst->rid.direct == rid.direct &&
        inst->rs2.ptag == ptag;
    };
    if (m_input_latch.valid && match(m_input_latch.inst)) {
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && match(m_pipe[i].inst)) {
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
}

void STD::commit()
{
    shift();
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}


void STD::reset()
{
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void STD::dump_states() const
{

}
