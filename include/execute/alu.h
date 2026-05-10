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
    explicit ALU(const ALUConfig& conf);
    ~ALU() override = default;

    void                                    build() override {}
    bool                                    dispatch(const MInstPtr inst);
    bool                                    accept() const;

    void                                    set_prf(PRF* prf) { m_prf = prf; }
    void                                    set_exec(void (*fn)(MInstPtr)) { exec = fn; }

    void                                    tick() override;
    void                                    commit() override;

public:
    void                                    set_stages(i32 stages) { m_stages = stages; }
    void                                    set_latency(u32 lat) { m_latency = lat; }

protected:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();
    virtual void                            E1();
    virtual void                            flush();

    void                                    shift();

    virtual void                            wakeup(MInstPtr inst);
    void                                    broadcast(MInstPtr inst);

protected:
    i32                                     m_stages { 0 };
    FUStage                                 m_pipe[MAX_STAGES];
    u32                                     m_latency { 1 };
    PRF*                                    m_prf { nullptr };

    void                                    (*exec)(MInstPtr inst) = nullptr;

    OUTPUT std::vector<WakeupEvent>         m_wakeup_q;
    OUTPUT std::vector<RegReq>              m_reg_read_q;
    OUTPUT std::vector<RegReq>              m_reg_write_q;

    INPUT  std::vector<RegReq>              m_reg_resp;
    INPUT  std::vector<FlushEvent>          m_flush_q;
};

#endif
