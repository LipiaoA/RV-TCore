#ifndef RV_TCORE_STD_H
#define RV_TCORE_STD_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"
#include "rob/rob.h"
#include "perf/perf_def.h"

class LSU;
class ExecuteUnit;

class STD : public DevIntf {
    friend class ExecuteUnit;
    constexpr static u32 MAX_STAGES = 8;
public:
    explicit STD(const STDConfig& conf, const STDDeps& deps);
    ~STD() override = default;

    void                                    build() override {}
    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

    void                                    tick() override;
    void                                    commit() override;

    void                                    flush(const FlushEvent& event);
    void                                    squash_rid(const ROBID& rid, i32 ptag);
    void                                    reset();

    void                                    set_idx(u32 idx) {m_idx = idx;}
    u32                                     get_idx() const {return m_idx;}
    FuType                                  get_fu_type() const {return m_fu_type;}

private:
    void                                    R1();
    void                                    E0();
    void                                    shift();
    void                                    dump_states() const;

    void                                    fwd_bypass();

    FUStage                                 m_input_latch;
    i32                                     m_stages { 2 };
    FUStage                                 m_pipe[MAX_STAGES];
    OUTPUT ASyncQueue<RegReq>&              m_reg_req_q;

    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_resp_q;
    INPUT  FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>&             m_reg_bypass_bus;
    ROB&                                    m_rob;
    LSU&                                    m_lsu;
    u32                                     m_idx;
    static u32                              std_idx;
    FuType                                  m_fu_type { FuType::STD };
    PerfStats&                              m_stats;
};

#endif
