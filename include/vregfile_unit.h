#ifndef RV_TCORE_VREGFILE_UNIT_H
#define RV_TCORE_VREGFILE_UNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"
#include "rename/vprf.h"

class VRegFileUnit {
public:
    VRegFileUnit(const VRegFileConfig& conf, const VRegFileDeps& deps);
    ~VRegFileUnit() = default;

    void tick();
    void build() {}
    void commit();
    bool is_ready(i32 ptag) const;
    void set_busy(i32 ptag);
    void reset();

private:
    void r0();

    u32 m_reg_capacity;
    VPRF m_vprf;

    INPUT ASyncQueue<VecRegReq>& m_vec_reg_req_q;
    OUTPUT FixedArray<VecRegResp, MAX_REGISTER_REQ_CAPACITY>& m_vpu_resp_q;
    OUTPUT std::vector<WakeupEvent>& m_wakeup_bus;
};

#endif
