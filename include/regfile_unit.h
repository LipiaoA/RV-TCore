#ifndef RV_TCORE_REGFILE_UNIT_H
#define RV_TCORE_REGFILE_UNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"
#include "rename/prf.h"

class RegFileUnit {
    constexpr static u32 MAX_RESP_WIDTH = 32;
    public:
    RegFileUnit(const RegFileConfig& conf, const RegFileDeps& deps);
    ~RegFileUnit() = default;
public:
    void                                tick();
    void                                build();
    void                                commit();
    void                                dispatch(MInstPtr inst);
    void                                accept() const;
    bool                                is_ready(i32 ptag) const;
    void                                set_busy(i32 ptag);
    void                                reset();

private:
    void                                flush();
    void                                r0();

private:
    u32                                 m_reg_width;
    u32                                 m_reg_capacity;
    PRF                                 m_prf;

    INPUT  ASyncQueue<RegReq>&          m_reg_req_q;

    OUTPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&         m_alu_resp_q;
    OUTPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&         m_bru_resp_q;
    OUTPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&         m_lsu_resp_q;
    OUTPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&         m_fpu_resp_q;
    OUTPUT std::vector<WakeupEvent>&                               m_wakeup_bus;
};

#endif
