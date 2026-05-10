#ifndef RV_TCORE_AGU_H
#define RV_TCORE_AGU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

class PRF;

class AGU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit AGU(const AGUConfig& conf);
    ~AGU() override = default;

    void                                    build() override {}
    void                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    set_prf(PRF* prf) { m_prf = prf; }
    void                                    set_exec(void (*fn)(MInstPtr)) { exec = fn; }
    void                                    set_latency(u32 lat) { m_latency = lat; }

    void                                    tick() override;
    void                                    commit() override {}

public:
    void                                    set_stages(i32 stages) { m_stages = stages; }

private:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();

    void                                    shift();
    void                                    flush();
    void                                    (*exec)(MInstPtr inst) = nullptr;
    void                                    broadcast(MInstPtr inst);
    StoreReq                                genStoreReq(const MicroOp& uop);
    LoadReq                                 genLoadReq(const MicroOp& uop);

private:
    i32                                     m_stages { 0 };
    FUStage                                 m_pipe[MAX_STAGES];
    u32                                     m_latency { 1 };
    PRF*                                    m_prf { nullptr };
    std::vector<SQEntry>*                   lsu_st_q { nullptr };
    std::vector<LQEntry>*                   lsu_ld_q { nullptr };
    OUTPUT std::vector<RegReq>*             m_reg_read_q { nullptr };

    INPUT  std::vector<RegReq>              m_reg_resp;

    OUTPUT std::vector<StoreReq>*           m_store_req_q { nullptr };
    OUTPUT std::vector<LoadReq>*            m_load_req_q { nullptr };
};

#endif
