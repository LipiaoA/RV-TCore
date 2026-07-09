#ifndef RV_TCORE_ISSUEUNIT_H
#define RV_TCORE_ISSUEUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "decode/inst.h"
#include <deque>
#include <unordered_set>
#include "config.h"
#include "rob/rob.h"

class RegFileUnit;

struct IssueCand {
    MInstPtr inst { nullptr };
    StoreIssueHalf store_half { StoreIssueHalf::NONE };
};

class IssueUnit : public IIssue {
public:
    IssueUnit(const IssueConfig& conf, const IssueDeps& deps);
    ~IssueUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    void                                wakeup();
    void                                select();
    void                                issue();
    void                                reset();

private:
    void                                async();
    void                                flush();
    void                                EmitSrcRead(const MInstPtr inst, const Operand& src, int idx);
    void                                EmitVecSrcRead(const MInstPtr inst, const Operand& src, int idx);
    void                                EmitTileSrcRead(const MInstPtr inst, const Operand& src, int idx);
    FuType                              GetFuType(const MInstPtr inst, StoreIssueHalf half);
    void                                retire_executed(std::vector<MInstPtr>& inst_q);
    bool                                in_issue_q(const ROBID& rid) const;
    void                                requeue_from_rob(MInstPtr inst);
    void                                apply_replay_to_inst(MInstPtr inst, i32 ptag, bool kill_sta, bool kill_std);
    void                                handle_dep_kill(const WakeupEvent& event);
    void                                clean_bypass_hint();

private:
    std::vector<MInstPtr>               m_int_q;
    std::vector<MInstPtr>               m_ls_q;
    std::vector<MInstPtr>               m_fp_q;
    std::vector<MInstPtr>               m_vec_q;
    std::vector<MInstPtr>               m_tensor_q;
    ROB&                                m_rob;
    ExecuteUnit&                        m_exec;
    INPUT ASyncQueue<MInstPtr>&         m_rename_issue_q;
    INPUT FlushEvent&                   m_flush_event;
    OUTPUT ASyncQueue<RegReq>&          m_reg_req_q;
    OUTPUT ASyncQueue<VecRegReq>&       m_vec_reg_req_q;
    OUTPUT ASyncQueue<TileRegReq>&      m_tile_reg_req_q;
    std::vector<IssueCand>              m_select_buffer;
    RegFileUnit*                        m_regfile { nullptr };
    LSU*                                m_lsu { nullptr };
    PerfStats*                          m_stats { nullptr };

    size_t                              m_int_issue_width  { 4 };
    size_t                              m_ls_issue_width   { 2 };
    size_t                              m_fp_issue_width   { 2 };
    size_t                              m_vec_issue_width  { 1 };
    size_t                              m_tile_issue_width { 1 };
    INPUT std::vector<WakeupEvent>&     m_wakeup_bus;
};

#endif
