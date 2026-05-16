#ifndef RV_TCORE_ALU_H
#define RV_TCORE_ALU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

class PRF;

// Integer ALU: 1-cycle logical latency, 3-stage pipe (E0/E1/E2)
class ALU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit ALU(const ALUConfig& conf, const ALUDeps& deps);
    ~ALU() override = default;

    void                                    build() override {}
    bool                                    dispatch(const MInstPtr inst);
    bool                                    accept() const;

    void                                    set_prf(PRF* prf) { m_prf = prf; }

    void                                    tick() override;
    void                                    commit() override;

public:

protected:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();
    virtual void                            E1();
    virtual void                            flush(const FlushEvent& event);

    void                                    shift();

    virtual void                            wakeup(MInstPtr inst);
    void                                    broadcast(MInstPtr inst);

private:
    FUStage                                 m_input_latch;
    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    PRF*                                    m_prf { nullptr };

    OUTPUT std::vector<WakeupEvent>         m_wakeup_q;
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& m_reg_bypass_q;
};

#endif
