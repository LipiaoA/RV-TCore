#ifndef RV_TCORE_FPU_H
#define RV_TCORE_FPU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "inst_exec.h"
#include "rob/rob.h"

class FPU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit FPU(const FPUConfig& conf, const FPUDeps& deps);
    ~FPU() override = default;

    void                                    tick() override;
    void                                    commit() override;
    void                                    build() override {}
    bool                                    dispatch(const MInstPtr inst);
    bool                                    accept() const;

    bool                                    commit_ready() const;
    MInstPtr                                commit_inst() const;
    void                                    commit_ack();

    void                                    flush(const FlushEvent& event);
    void                                    reset() override;

private:
    void                                    R1();
    void                                    E0();
    void                                    shift();
    void                                    wakeup(MInstPtr inst);

private:
    FUStage                                 m_input_latch;
    FUStage                                 m_commit_latch;
    i32                                     m_stages { 2 };
    ROB&                                    m_rob;
    OUTPUT std::vector<WakeupEvent>&        m_wakeup_bus;
    INPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&              m_reg_resp_q;
    INPUT FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&              m_reg_bypass_bus;
    FUStage                                 m_pipe[MAX_STAGES];
};

#endif
