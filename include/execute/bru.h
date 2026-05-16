#ifndef RV_TCORE_BRU_H
#define RV_TCORE_BRU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

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

private:
    void                                    R0();
    void                                    R1();
    void                                    E0();
    void                                    flush(const FlushEvent& event);

    void                                    shift();

    void                                    send_redirect(pc_t target);
    void                                    send_flush(MInstPtr inst);
    void                                    dump_states() const;

private:
    FUStage                                 m_input_latch;

    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    bool                                    m_pred_taken { false };
    pc_t                                    m_pred_target { 0 };

    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;
    OUTPUT ASyncQueue<FlushEvent>&          m_flush_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_q;
};

#endif
