#include "agu.h"
#include "inst_exec.h"

u32 AGU::agu_idx = 0;

AGU::AGU(const AGUConfig& conf, const AGUDeps& deps)
    : DevIntf("agu")
    , m_stages(conf.stages)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
{
    for (i32 i = 0; i < m_stages; i++) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_idx = AGU::agu_idx++;
}

void AGU::tick()
{
    E0();
    R1();
    dump_states();
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

void AGU::R1()
{
    if (!m_pipe[0].valid || m_reg_resp_q.empty())
        return;
    MInstPtr inst = m_pipe[0].inst;
    if (inst->rs1.valid) {
        bool found = false;
        for (auto& resp : m_reg_bypass_bus) {
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
    if (!m_pipe[1].valid || !m_pipe[1].inst)
        return;
    MInstPtr inst = m_pipe[1].inst;
    InstExec::AguExecResult ret = InstExec::exec_agu(inst);
    if (!ret.valid)
        return;
    broadcast(inst);

    if (inst->type == InstType::STA || inst->type == InstType::STORE) {
        if (lsu_st_q) {
            auto& st_entry = lsu_st_q->at(static_cast<size_t>(inst->store_idx));
            st_entry.addr = ret.addr;
            st_entry.addr_ready = true;
        }
    } else if (inst->type == InstType::LOAD) {
        if (lsu_ld_q) {
            auto& ld_entry = lsu_ld_q->at(static_cast<size_t>(inst->load_idx));
            ld_entry.addr = ret.addr;
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
}

void AGU::flush(const FlushEvent& event)
{

}

void AGU::broadcast(MInstPtr inst)
{
    return;
}


StoreReq AGU::genStoreReq(MInstPtr inst) {
    StoreReq req;
    req.valid = false;
    return req;
}

LoadReq AGU::genLoadReq(MInstPtr inst) {
    LoadReq req;
    req.valid = false;
    return req;
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
    return;
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
    if (m_pipe[3].valid) {
        auto inst = m_pipe[3].inst;
        LOG_LEVEL1(log_prefix + "E0", "rd: v%d-a%d-p%d-0x%lx",
            inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val);
    }
}