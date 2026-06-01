#ifndef RV_TCORE_FPU_H
#define RV_TCORE_FPU_H

#include "alu.h"
#include "inst_exec.h"

class FPU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    FPU(const FPUConfig& conf, const FPUDeps& deps)
    : DevIntf("fpu")
    , m_stages(conf.stages)
    , m_reg_req_q(*deps.reg_req_q)
    , m_reg_resp_q(*deps.reg_resp_q)
    , m_reg_bypass_bus(*deps.reg_bypass_bus)
    , m_flush_event(*deps.flush_event)
    {
        for (i32 i = 0; i < m_stages; i++) {
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
    ~FPU() = default;

    void                                    tick() {};
    void                                    commit() {}
    void                                    build() {}
    void                                    flush(const FlushEvent& event) {}
    void                                    reset() {}

private:
    i32                                     m_stages { 4 };
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;
    INPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&              m_reg_resp_q;
    INPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&              m_reg_bypass_bus;
    FUStage                                 m_pipe[MAX_STAGES];
    FlushEvent&                             m_flush_event;
};

#endif
