#include "vregfile_unit.h"

VRegFileUnit::VRegFileUnit(const VRegFileConfig& conf, const VRegFileDeps& deps)
    : m_reg_capacity(conf.capacity)
    , m_vprf(conf.capacity, conf.vlmax_bytes)
    , m_vec_reg_req_q(*deps.vec_reg_req_q)
    , m_vpu_resp_q(*deps.vpu_resp_q)
    , m_wakeup_bus(*deps.wakeup_bus)
{
}

void VRegFileUnit::tick()
{
    r0();
}

void VRegFileUnit::commit()
{
    m_vec_reg_req_q.commit();
}

void VRegFileUnit::r0()
{
    m_vpu_resp_q.clear();

    while (!m_vec_reg_req_q.empty()) {
        VecRegReq req;
        if (!m_vec_reg_req_q.read(req) || !req.valid) {
            continue;
        }
        if (req.write) {
            if (m_vprf.write(req.ptag, req.val)) {
                WakeupEvent event;
                event.valid = true;
                event.type = WAKEUP_WRITEBACK;
                event.ptag = req.ptag;
                event.rid = req.rid;
                event.rtyp = RegType::VEC;
                m_wakeup_bus.push_back(event);
            }
        } else {
            VecRegResp resp;
            resp.valid = true;
            resp.ptag = req.ptag;
            resp.val = m_vprf.read(req.ptag);
            resp.rid = req.rid;
            resp.idx = req.idx;
            resp.fu = req.fu;
            m_vpu_resp_q.push_back(resp);
        }
    }
}

bool VRegFileUnit::is_ready(i32 ptag) const
{
    return m_vprf.is_ready(ptag);
}

void VRegFileUnit::set_busy(i32 ptag)
{
    m_vprf.set_busy(ptag);
}

void VRegFileUnit::reset()
{
    m_vprf.reset();
}
