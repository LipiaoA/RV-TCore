#ifndef RV_TCORE_AGU_H
#define RV_TCORE_AGU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

typedef struct {
    ASyncQueue<RegReq>*         reg_req_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus;
} AGUDeps;

typedef struct {
    u32 stages { 4 };
} AGUConfig;

class AGU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit AGU(const AGUConfig& conf, const AGUDeps& deps);
    ~AGU() override = default;

    void                                    build() override {}
    void                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    void                                    dump_states() const;

private:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();

    void                                    shift();
    void                                    flush(const FlushEvent& event);
    void                                    broadcast(MInstPtr inst);
    StoreReq                                genStoreReq(MInstPtr inst);
    LoadReq                                 genLoadReq(MInstPtr inst);

private:
    FUStage                                 m_input_latch;
    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    std::vector<SQEntry>*                   lsu_st_q { nullptr };
    std::vector<LQEntry>*                   lsu_ld_q { nullptr };
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    OUTPUT std::vector<StoreReq>*           m_store_req_q { nullptr };
    OUTPUT std::vector<LoadReq>*            m_load_req_q { nullptr };

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;
    u32                                     m_idx { 0 };
    static u32                              agu_idx;
};

#endif
