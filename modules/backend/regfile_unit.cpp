#include "regfile_unit.h"

RegFileUnit::RegFileUnit(const RegFileConfig& conf, const RegFileDeps& deps)
    : m_reg_width(conf.width),
      m_reg_capacity(conf.capacity),
      m_prf(conf.capacity),
      m_reg_req_q(*deps.reg_req_q),
      m_alu_resp_q(*deps.alu_resp_q),
      m_bru_resp_q(*deps.bru_resp_q),
      m_lsu_resp_q(*deps.lsu_resp_q),
      m_fpu_resp_q(*deps.fpu_resp_q),
      m_wakeup_bus(*deps.wakeup_bus)
{
}

void RegFileUnit::build() {

}

void RegFileUnit::tick() {
    r0();
}

void RegFileUnit::commit() {
    m_reg_req_q.commit();
}

void RegFileUnit::dispatch(MInstPtr inst) {
}

void RegFileUnit::accept() const {
}

void RegFileUnit::r0() {
    auto invalidate = [](FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& resp_q) {
        resp_q.clear();
    };
    invalidate(m_bru_resp_q);
    invalidate(m_lsu_resp_q);
    invalidate(m_fpu_resp_q);
    invalidate(m_alu_resp_q);

    while (!m_reg_req_q.empty()) {
        RegResp resp;
        RegReq req;
        bool ret = m_reg_req_q.read(req);
        if (!ret || !req.valid)
            continue;
        if (req.write) {
            if (m_prf.write(req.ptag, req.val)) {
                WakeupEvent event;
                event.valid = true;
                event.type = WAKEUP_WRITEBACK;
                event.ptag = req.ptag;
                event.rid = req.rid;
                event.rtyp = RegType::GPR;
                m_wakeup_bus.push_back(event);
            }
        } else {
            resp.valid = true;
            resp.ptag = req.ptag;
            resp.val = m_prf.read(req.ptag);
            resp.rid = req.rid;
            resp.idx = req.idx;
            resp.fu = req.fu;
            if (req.fu == FuType::ALU) {
                m_alu_resp_q.push_back(resp);
            } else if (req.fu == FuType::BRU) {
                m_bru_resp_q.push_back(resp);
            } else if (req.fu == FuType::LSU || req.fu == FuType::AGU || req.fu == FuType::STD) {
                m_lsu_resp_q.push_back(resp);
            } else if (req.fu == FuType::FPU) {
                m_fpu_resp_q.push_back(resp);
            } else {
                assert(0);
            }
        }
    }
}

bool RegFileUnit::is_ready(i32 ptag) const {
    return m_prf.is_ready(ptag);
}

void RegFileUnit::set_busy(i32 ptag) {
    m_prf.set_busy(ptag);
}

void RegFileUnit::init_phys(i32 ptag, u64 val) {
    m_prf.write(ptag, val);
}

void RegFileUnit::reset() {
    m_prf.reset();
}

void RegFileUnit::flush() {
}
