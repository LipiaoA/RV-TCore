#ifndef RV_TCORE_STD_H
#define RV_TCORE_STD_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"

class LSU;
class ExecuteUnit;

class STD : public DevIntf {
    friend class ExecuteUnit;
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit STD(const STDConfig& conf, const STDDeps& deps);
    ~STD() override = default;

    void                                    build() override {}
    bool                                    dispatch(const IssueRequest& request);
    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    void                                    flush(const FlushEvent& event);
    void                                    reset();

private:
    void                                    R1();
    void                                    E0();
    void                                    shift();
    void                                    dump_states() const;

    void                                    fwd_bypass();
    bool                                    capture_operand(IssueStage& stage);
    bool                                    capture_from_bypass(Operand& src);
    bool                                    capture_from_regfile(Operand& src);
    bool                                    send_feedback(
                                                const IssueRequest& request,
                                                FuAcceptResult result);

    IssueStage                              m_input_latch;
    i32                                     m_stages { 2 };
    IssueStage                              m_pipe[MAX_STAGES];
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;
    OUTPUT ASyncQueue<FuAcceptEvent>&         m_fu_accept_q;
    ROB&                                    m_rob;
    LSU&                                    m_lsu;
    u32                                     m_idx { 0 };
    static u32                              std_idx;
};

#endif
