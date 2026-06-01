#ifndef RV_TCORE_BRU_H
#define RV_TCORE_BRU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"

class BRU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    BRU(const BRUConfig& conf, const BRUDeps& deps);
    ~BRU() override = default;

    void                                    tick() override;
    void                                    commit() override;
    void                                    build() override {}

    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    bool                                    commit_ready() const;
    MInstPtr                                commit_inst() const;
    void                                    commit_ack();

    void                                    flush(const FlushEvent& event);
    void                                    reset();

private:
    void                                    R1();
    void                                    E0();

    void                                    shift();

    void                                    send_redirect(pc_t target);
    void                                    send_flush(MInstPtr inst);
    void                                    dump_states() const;
    void                                    wakeup(MInstPtr inst);

private:
    FUStage                                 m_input_latch;
    FUStage                                 m_commit_latch;
    ROB&                                    m_rob;

    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    bool                                    m_pred_taken { false };
    pc_t                                    m_pred_target { 0 };

    OUTPUT ASyncQueue<FlushEvent>&          m_flush_req_q;
    OUTPUT std::vector<WakeupEvent>&        m_wakeup_bus;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;

    u32                                     m_idx { 0 };
    static u32                              bru_idx;
};

#endif
