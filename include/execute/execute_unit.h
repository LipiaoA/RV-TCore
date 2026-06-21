#ifndef RV_TCORE_EXECUTEUNIT_H
#define RV_TCORE_EXECUTEUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "alu.h"
#include "agu.h"
#include "stx.h"
#include "lsu.h"
#include "fpu.h"
#include "execute/vector/vector_unit.h"
#include "execute/tile/tile_unit.h"
#include "bru.h"
#include "rv_def.h"
#include "config.h"

class ExecuteUnit : public IExecute {
    constexpr static u32 MAX_ALU_NUM = 8;
    constexpr static u32 MAX_FPU_NUM = 4;
    constexpr static u32 MAX_BRU_NUM = 4;
    constexpr static u32 MAX_AGU_NUM = 4;
    constexpr static u32 MAX_STD_NUM = 4;
    constexpr static u32 MAX_VPU_NUM = 2;
    constexpr static u32 MAX_TPU_NUM = 2;
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
    FixedStorage<AGU, MAX_AGU_NUM>      m_agus;
    FixedStorage<STD, MAX_STD_NUM>      m_stds;
    FixedStorage<VectorUnit, MAX_VPU_NUM> m_vpus;
    FixedStorage<TileUnit, MAX_TPU_NUM>   m_tpus;
    LSU&                                m_lsu;

    ALUConfig                           m_alu_conf;
    FPUConfig                           m_fpu_conf;
    BRUConfig                           m_bru_conf;
    AGUConfig                           m_agu_conf;
    STDConfig                           m_std_conf;

    u32                                 m_alu_num { 0 };
    u32                                 m_fpu_num { 0 };
    u32                                 m_vpu_num { 0 };
    u32                                 m_tpu_num { 0 };
    u32                                 m_bru_num { 0 };
    u32                                 m_agu_num { 0 };
    u32                                 m_std_num { 0 };
    u32                                 m_commit_width { 4 };
    u32                                 m_next_alu { 0 };
    u32                                 m_next_fpu { 0 };
    u32                                 m_next_vpu { 0 };
    u32                                 m_next_tpu { 0 };
    u32                                 m_next_bru { 0 };
    u32                                 m_next_agu { 0 };
    u32                                 m_next_std { 0 };
    ASyncQueue<MInstPtr>&               m_inst_commit_q;
    FlushEvent&                         m_flush_event;
    ROB&                                m_rob;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>& m_reg_bypass_bus;
    PerfStats*                          m_stats { nullptr };
};

#endif
