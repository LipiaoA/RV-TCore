#ifndef RV_TCORE_TREGFILE_UNIT_H
#define RV_TCORE_TREGFILE_UNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"
#include "rename/tprf.h"

class TRegFileUnit {
public:
    TRegFileUnit(const TRegFileConfig& conf, const TRegFileDeps& deps);
    ~TRegFileUnit() = default;

    void tick();
    void build() {}
    void commit();
    bool is_ready(i32 ptag) const;
    void set_busy(i32 ptag);
    void reset();

private:
    void r0();

    u32 m_reg_capacity;
    TPRF m_tprf;

    INPUT ASyncQueue<TileRegReq>& m_tile_reg_req_q;
    OUTPUT FixedArray<TileRegResp, MAX_REGISTER_REQ_CAPACITY>& m_tpu_resp_q;
    OUTPUT std::vector<WakeupEvent>& m_wakeup_bus;
};

#endif
