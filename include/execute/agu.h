#ifndef RV_TCORE_AGU_H
#define RV_TCORE_AGU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"

class LSU;

class AGU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit AGU(const AGUConfig& conf, const AGUDeps& deps);
    ~AGU() override = default;

    void                                    build() override {}
    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    void                                    dump_states() const;
    void                                    flush(const FlushEvent& event);
    void                                    reset();

private:
    bool                                    R1();
    void                                    E0();

    void                                    shift(bool r1_ready);
    bool                                    read_reg(Operand& src);

private:
    FUStage                                 m_input_latch;
    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    std::vector<SQEntry>&                   lsu_st_q;
    std::vector<LQEntry>&                   lsu_ld_q;
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;
    ROB&                                    m_rob;
    LSU&                                    m_lsu;
    u32                                     m_idx { 0 };
    static u32                              agu_idx;
};

#endif
