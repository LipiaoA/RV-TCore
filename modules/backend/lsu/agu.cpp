#include "agu.h"
#include "inst_exec.h"

AGU::AGU(const AGUConfig& conf, const AGUDeps& deps)
    : DevIntf("agu")
    , m_stages(conf.stages)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_q(*deps.reg_bypass_q)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}

void AGU::tick()
{
    E0();
    R1();
    R0();
    shift();
}

bool AGU::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

void AGU::dispatch(MInstPtr inst)
{
    if (!accept() || !inst)
        return;
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
}

void AGU::R0()
{
    if (!m_pipe[0].valid)
        return;
    RegReq req;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid) {
        req.valid = true;
        req.ptag = inst->rs1.ptag;
        req.write = false;
        m_reg_req_q.write(req);
    }
}

void AGU::R1()
{
    if (!m_pipe[1].valid || m_reg_resp_q.empty())
        return;
    MInstPtr inst = m_pipe[1].inst;
    if (inst->rs1.valid) {
        bool found = false;
        for (auto& resp : m_reg_bypass_q) {
            if (resp.valid && resp.ptag == inst->rs1.ptag) {
                inst->rs1.val = resp.val;
                found = true;
                break;
            }
        }
        if (!found) {
            for (auto& resp : m_reg_resp_q) {
                if (resp.valid && resp.ptag == inst->rs1.ptag) {
                    inst->rs1.val = resp.val;
                    break;
                }
            }
        }
    }
}

void AGU::E0()
{
    if (!m_pipe[3].valid)
        return;
    MInstPtr inst = m_pipe[3].inst;
    bool ret = InstExec::exec(inst);
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
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
        }
    }
    m_pipe[0].valid = false;
    m_pipe[0].inst  = nullptr;
}

void AGU::flush(const FlushEvent& event)
{

}

void AGU::broadcast(MInstPtr inst)
{
    return;
}


StoreReq AGU::genStoreReq(MInstPtr inst) {

}

LoadReq AGU::genLoadReq(MInstPtr inst) {
    LoadReq req;
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
