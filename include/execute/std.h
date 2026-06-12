#ifndef RV_TCORE_STD_H
#define RV_TCORE_STD_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"

class LSU;

typedef struct STDDeps {
    ASyncQueue<RegReq>*         reg_req_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus;
    ROB*                        rob;
} STDDeps;

class STD : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit STD(const STDConfig& conf, const STDDeps& deps);
    ~STD() override = default;

    void                                    build() override {}
    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    bool                                    commit_ready() const;
    MInstPtr                                commit_inst() const;
    void                                    commit_ack();

    void                                    bind_lsu(LSU* lsu);
    void                                    flush(const FlushEvent& event);
    void                                    reset();

private:
    bool                                    R1();
    void                                    E0();
    void                                    shift(bool r1_ready);
    bool                                    read_reg(Operand& src);
    void                                    dump_states() const;

private:
    FUStage                                 m_input_latch;
    FUStage                                 m_commit_latch;
    i32                                     m_stages { 2 };
    FUStage                                 m_pipe[MAX_STAGES];
    LSU*                                    m_lsu { nullptr };
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;
    ROB&                                    m_rob;
    u32                                     m_idx { 0 };
    static u32                              std_idx;
};

#endif
