#include "tregfile_unit.h"

TRegFileUnit::TRegFileUnit(const TRegFileConfig& conf, const TRegFileDeps& deps)
    : m_reg_capacity(conf.capacity)
    , m_tprf(conf.capacity, conf.tile_rows, conf.tile_cols, conf.elem_bits)
    , m_tile_reg_req_q(*deps.tile_reg_req_q)
    , m_tpu_resp_q(*deps.tpu_resp_q)
    , m_wakeup_bus(*deps.wakeup_bus)
{
}

void TRegFileUnit::tick()
{
    r0();
}

void TRegFileUnit::commit()
{
    m_tile_reg_req_q.commit();
}

void TRegFileUnit::r0()
{
    m_tpu_resp_q.clear();

    while (!m_tile_reg_req_q.empty()) {
        TileRegReq req;
        if (!m_tile_reg_req_q.read(req) || !req.valid) {
            continue;
        }
        if (req.write) {
            if (m_tprf.write(req.ptag, req.val)) {
                WakeupEvent event;
                event.valid = true;
                event.type = WAKEUP_WRITEBACK;
                event.ptag = req.ptag;
                event.rid = req.rid;
                event.rtyp = RegType::TILE;
                m_wakeup_bus.push_back(event);
            }
        } else {
            TileRegResp resp;
            resp.valid = true;
            resp.ptag = req.ptag;
            resp.val = m_tprf.read(req.ptag);
            resp.rid = req.rid;
            resp.idx = req.idx;
            resp.fu = req.fu;
            m_tpu_resp_q.push_back(resp);
        }
    }
}

bool TRegFileUnit::is_ready(i32 ptag) const
{
    return m_tprf.is_ready(ptag);
}

void TRegFileUnit::set_busy(i32 ptag)
{
    m_tprf.set_busy(ptag);
}

void TRegFileUnit::reset()
{
    m_tprf.reset();
}
