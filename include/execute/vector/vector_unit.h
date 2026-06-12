#ifndef RV_TCORE_VECTORUNIT_H
#define RV_TCORE_VECTORUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"
#include "rob/rob.h"

class VectorUnit : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    VectorUnit(const VectorConfig& conf, const VectorDeps& deps);
    ~VectorUnit() override = default;

    void tick() override;
    void commit() override;
    void build() override {}
    bool dispatch(const MInstPtr inst);
    bool accept() const;
    bool commit_ready() const;
    MInstPtr commit_inst() const;
    void commit_ack();
    void flush(const FlushEvent& event);
    void reset() override;

private:
    void R1();
    void E0();
    void shift();
    void wakeup(MInstPtr inst);
    void exec_vector(MInstPtr inst);

    FUStage m_input_latch;
    FUStage m_commit_latch;
    i32 m_stages { 2 };
    ROB& m_rob;
    OUTPUT std::vector<WakeupEvent>& m_wakeup_bus;
    INPUT FixedArray<VecRegResp, MAX_REGISTER_REQ_CAPACITY>& m_vpu_resp_q;
    FUStage m_pipe[MAX_STAGES];
};

#endif
