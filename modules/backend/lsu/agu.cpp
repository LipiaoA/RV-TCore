#include "agu.h"

#include <cassert>

AGU::AGU(const AGUConfig& conf)
    : DevIntf("agu")
    , m_stages(conf.stages)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void AGU::tick()
{
    flush();
    E0();
    R1();
    R0();
    shift();
}

bool AGU::accept() const
{
    return !m_pipe[0].valid;
}

void AGU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst)
        return;
    m_pipe[0].inst  = inst;
    m_pipe[0].valid = true;
}

void AGU::R0()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst)
        return;
    RegReq req;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid) {
        req.valid = true;
        req.ptag = inst->rs1.ptag;
        req.write = false;
        if (m_reg_read_q)
            m_reg_read_q->push_back(req);
    }
}

void AGU::R1()
{
    if (!m_pipe[1].valid || !m_pipe[1].inst || m_reg_resp.empty())
        return;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid && m_reg_resp[0].valid) {
        inst->rs1.val = m_reg_resp[0].val;
    }
}

void AGU::E0()
{
    if (!m_pipe[3].valid || !m_pipe[3].inst)
        return;
    MInstPtr inst = m_pipe[3].inst;
    if (exec)
        exec(inst);
    broadcast(inst);

    if (inst->type == InstType::STA) {
        if (lsu_st_q) {
            auto& st_entry = lsu_st_q->at(static_cast<size_t>(inst->store_idx));
            st_entry.addr = inst->rs1.val;
            st_entry.addr_ready = true;
        }
    } else if (inst->type == InstType::LOAD) {
        if (lsu_ld_q) {
            auto& ld_entry = lsu_ld_q->at(static_cast<size_t>(inst->load_idx));
            ld_entry.addr = inst->rs1.val;
            ld_entry.addr_ready = true;
        }
    } else {
        assert(0);
    }
}

void AGU::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i]            = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
    m_pipe[0].valid = false;
    m_pipe[0].inst  = nullptr;
}

void AGU::flush()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i]            = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
}

void AGU::broadcast(MInstPtr inst)
{
    (void)inst;
}

StoreReq AGU::genStoreReq(const MicroOp& uop)
{
    (void)uop;
    return {};
}

LoadReq AGU::genLoadReq(const MicroOp& uop)
{
    (void)uop;
    return {};
}
