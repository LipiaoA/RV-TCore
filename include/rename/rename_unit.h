#ifndef RV_TCORE_RENAMEUNIT_H
#define RV_TCORE_RENAMEUNIT_H

#include <deque>
#include <vector>
#include <array>

#include "intf.h"
#include "config.h"
#include "decode/inst.h"
#include "module_comm.h"
#include "rename/prf.h"
#include "rename/checkpoint.h"

class ROB;
class LSU;
class VRegFileUnit;
class TRegFileUnit;

class RenameUnit : public IRename {
    constexpr static u32 MAX_CHECKPOINT_DEPTH = 6;
public:
    RenameUnit(const RenameConfig& conf, const RenameDeps& deps);
    ~RenameUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    void                                reset() override;

    bool                                can_dispatch(MInstPtr inst) const;
    bool                                dispatch(MInstPtr inst);
    void                                recycle_phys(int phys);
    void                                recycle_vec_phys(int phys, u32 group = 1);
    void                                recycle_tile_phys(int phys);
    void                                init_rat(const std::array<i32, RV_GPR_SIZE>& rat);
    void                                init_vrat(const std::array<i32, RV_VREG_SIZE>& vrat);
    void                                init_trat(const std::array<i32, RV_TILE_SIZE>& trat);

private:
    void                                flush();
    void                                r0();
    void                                r1();
    void                                r2();
    void                                rebuild_free_list();
    void                                rebuild_vfree_list();
    void                                rebuild_tfree_list();

    void                                rename_src_gpr(Operand& src);
    void                                rename_src_vec(Operand& src);
    void                                rename_src_tile(Operand& src);

    void                                rename_inst_vec(MInstPtr inst);
    void                                rename_inst_tile(MInstPtr inst);

    void                                rename_rd_gpr(MInstPtr inst);
    void                                rename_rd_vec(MInstPtr inst);
    void                                rename_rd_tile(MInstPtr inst);

    bool                                lsq_resources_available(const MInstPtr& inst) const;
    bool                                rob_allocate(MInstPtr inst);
    bool                                lsq_allocate(MInstPtr inst);
    bool                                save_branch_checkpoint(MInstPtr inst);
    bool                                allocate_vec_group(Operand& dst, u32 group, u32 base_atag);
    bool                                needs_gpr_free_entry(const MInstPtr& inst) const;
    bool                                needs_vec_free_entry(const MInstPtr& inst) const;
    bool                                needs_tile_free_entry(const MInstPtr& inst) const;

    INPUT ASyncQueue<MInstPtr>&         m_decode_rename_q;
    OUTPUT ASyncQueue<MInstPtr>&        m_rename_issue_q;
    ROB&                                m_rob;
    RegFileUnit&                        m_regfile;
    VRegFileUnit&                       m_vregfile;
    TRegFileUnit&                       m_tregfile;
    LSU&                                m_lsu;
    std::array<i32, RV_GPR_SIZE>&       m_crat;
    std::array<i32, RV_VREG_SIZE>&      m_vcrat;
    std::array<i32, RV_TILE_SIZE>&      m_tcrat;
    std::vector<RatCheckpoint>&         m_checkpoints;

    FlushEvent&                         m_flush_event;
    PerfStats*                          m_stats { nullptr };

    std::array<i32, RV_GPR_SIZE>        m_rat;
    std::array<i32, RV_VREG_SIZE>       m_vrat;
    std::array<i32, RV_TILE_SIZE>       m_trat;
    std::deque<i32>                     m_free_list;
    std::deque<i32>                     m_vfree_list;
    std::deque<i32>                     m_tfree_list;

    std::deque<MInstPtr>                m_r1_buf;
    std::deque<MInstPtr>                m_r2_buf;

    size_t                              m_rename_width { 4 };

    u64                                 m_checkpoint_id { 0 };
    u32                                 m_prf_capacity { 0 };
    u32                                 m_vprf_capacity { 0 };
    u32                                 m_tprf_capacity { 0 };
};

#endif
