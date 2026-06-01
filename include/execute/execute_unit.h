#ifndef RV_TCORE_EXECUTEUNIT_H
#define RV_TCORE_EXECUTEUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "alu.h"
#include "lsu.h"
#include "fpu.h"
#include "bru.h"
#include "rv_def.h"
#include "config.h"

class ExecuteUnit : public IExecute {
    constexpr static u32 MAX_ALU_NUM = 8;
    constexpr static u32 MAX_FPU_NUM = 4;
    constexpr static u32 MAX_BRU_NUM = 4;
public:
    ExecuteUnit(const ExecConfig& conf, const ExecDeps& deps);
    ~ExecuteUnit() {}
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    void                                reset() override;

    bool                                dispatch(MInstPtr inst);

private:
    void                                arbitration();
    void                                flush();

private:
    FixedStorage<ALU, MAX_ALU_NUM>      m_alus;
    FixedStorage<FPU, MAX_FPU_NUM>      m_fpus;
    FixedStorage<BRU, MAX_BRU_NUM>      m_brus;
    LSU&                                m_lsu;

    ALUConfig                           m_alu_conf;
    FPUConfig                           m_fpu_conf;
    BRUConfig                           m_bru_conf;
    LSUConfig                           m_lsu_conf;

    u32                                 m_alu_num { 0 };
    u32                                 m_fpu_num { 0 };
    u32                                 m_bru_num { 0 };
    u32                                 m_lsu_width { 0 };
    u32                                 m_commit_width { 4 };
    ASyncQueue<MInstPtr>&               m_inst_commit_q;
    FlushEvent&                         m_flush_event;
    ROB&                                m_rob;
    PerfStats*                          m_stats { nullptr };
};

#endif
