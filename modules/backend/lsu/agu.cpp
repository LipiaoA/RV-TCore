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
    , m_lsu(*deps.lsu)
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

void AGU::fwd_bypass()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    for (Operand* src : {&inst->rs1, &inst->rs2}) {
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
}

void AGU::tick()
{
    E0();
    R1();
    dump_states();
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

void AGU::R1()
{
    MInstPtr inst = m_pipe[0].inst;
    if (!m_pipe[0].valid || !inst || !inst->rs1.valid) {
        return;
    }
    auto& src = inst->rs1;
    if (src.bypass_hint) {
        return;
    }
    // read from reg prf
    for (const auto& resp : m_reg_resp_q) {
        if (resp.valid && resp.ptag == src.ptag) {
            src.val = resp.val;
            break;
        }
    }
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
        SQEntry* st = m_lsu.find_store_by_id(inst->store_id);
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
        LOG_LEVEL1("AGU E0", "addr ready,store pc:0x%lx id:%lu addr:0x%lx rid:%s%u",
                   inst->pc, st_entry.store_id, st_entry.addr, inst->rid.direct ? "--" : "++", inst->rid.idx);
    } else if (inst->type == InstType::LOAD) {
        LQEntry* ld = m_lsu.find_load_by_id(inst->load_idx);
        if (!ld) {
            return;
        }
        ld->addr = ret.addr;
        ld->addr_ready = true;
        LOG_LEVEL1("AGU E0", "addr ready, load pc:0x%lx id:%lu addr:0x%lx rid:%s%u",
                   inst->pc, ld->load_id, ld->addr, inst->rid.direct ? "--" : "++", inst->rid.idx);
    }
    m_pipe[1].valid = false;
    m_pipe[1].inst = nullptr;
}

void AGU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
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
    shift();
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

}
