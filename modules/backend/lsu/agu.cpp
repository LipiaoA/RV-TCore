#include <cassert>

#include "agu.h"
#include "lsu.h"
#include "inst_exec.h"

u32 AGU::agu_idx = 0;

AGU::AGU(const AGUConfig& conf, const AGUDeps& deps)
    : DevIntf("agu")
    , m_stages(conf.stages)
    , lsu_ld_q(*deps.lsu_ld_q)
    , lsu_st_q(*deps.lsu_st_q)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
    , m_rob(*deps.rob)
{
    assert(m_stages >= 2 && m_stages <= static_cast<i32>(MAX_STAGES));
    for (i32 i = 0; i < static_cast<i32>(MAX_STAGES); i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    m_idx = AGU::agu_idx++;
}

void AGU::tick()
{
    E0();
    bool r1_ready = R1();
    dump_states();
    shift(r1_ready);
}

bool AGU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool AGU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst) {
        return false;
    }
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

bool AGU::read_reg(Operand& src)
{
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
}

bool AGU::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return true;
    }
    MInstPtr inst = m_pipe[0].inst;

    if (inst->rs1.valid && !read_reg(inst->rs1)) {
        return false;
    }
    return true;
}

void AGU::E0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst) {
        return;
    }
    MInstPtr inst = m_pipe[1].inst;

    InstExec::AguExecResult ret = InstExec::exec_agu(inst);
    if (!ret.valid) {
        return;
    }

    if (inst->type == InstType::STA) {
        SQEntry* st = m_lsu.find_store_by_id(inst->store_idx);
        if (!st) {
            return;
        }
        auto& st_entry = *st;
        st_entry.addr = ret.addr;
        st_entry.addr_ready = true;
        if (st_entry.ls_width == 0) {
            st_entry.ls_width = inst->ls_width;
        }
        m_lsu.on_store_operands_updated(st_entry);
    } else if (inst->type == InstType::LOAD) {
        LQEntry* ld = m_lsu.find_load_by_id(inst->load_idx);
        if (!ld) {
            return;
        }
        ld->addr = ret.addr;
        ld->addr_ready = true;
    }
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
}

void AGU::shift(bool r1_ready)
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (i == 1 && !r1_ready) {
            continue;
        }
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void AGU::flush(const FlushEvent& event)
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

void AGU::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0].inst = m_input_latch.inst;
        m_pipe[0].valid = true;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

void AGU::reset() {
    m_input_latch.valid = false;
    m_input_latch.inst = nullptr;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void AGU::dump_states() const
{
    std::string log_prefix = "AGU" + std::to_string(m_idx) + " ";
    if (m_pipe[0].valid) {
        auto inst = m_pipe[0].inst;
        LOG_LEVEL1(log_prefix + "R0", "rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
    if (m_pipe[1].valid) {
        auto inst = m_pipe[1].inst;
        LOG_LEVEL1(log_prefix + "R1", "rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx",
            inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
            inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val);
    }
}
