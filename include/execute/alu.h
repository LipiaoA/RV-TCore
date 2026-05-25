#ifndef RV_TCORE_ALU_H
#define RV_TCORE_ALU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"

// Integer ALU: 1-cycle logical latency, 3-stage pipe (E0/E1/E2)
class ALU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit ALU(const ALUConfig& conf, const ALUDeps& deps);
    ~ALU() override = default;

    void                                    build() override {}
    bool                                    dispatch(const MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    bool                                    commit_ready() const;
    MInstPtr                                commit_inst() const;
    void                                    commit_ack();

public:
    virtual void                            flush(const FlushEvent& event);

protected:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();

    void                                    dump_states() const;
    void                                    shift();

    virtual void                            wakeup(MInstPtr inst);
    void                                    broadcast(MInstPtr inst);

private:
    FUStage                                 m_input_latch;
    FUStage                                 m_commit_latch;
    i32                                     m_stages { 3 };
    FUStage                                 m_pipe[MAX_STAGES];
    ROB&                                    m_rob;

    OUTPUT std::vector<WakeupEvent>         m_wakeup_q;
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& m_reg_bypass_q;
    u32                                     m_idx { 0 };

    static u32                              alu_idx;
};

#endif
