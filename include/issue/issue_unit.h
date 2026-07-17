#ifndef RV_TCORE_ISSUEUNIT_H
#define RV_TCORE_ISSUEUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "decode/inst.h"
#include <deque>
#include "config.h"
#include "rob/rob.h"

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
    void                                handle_fu_feedback();
    void                                handle_store_feedback(const FuAcceptEvent& event);
    void                                handle_store_wakeup_replay(
                                            const WakeupEvent& event);
    void                                handle_non_store_wakeup_replay(
                                            const WakeupEvent& event);
    void                                apply_gpr_wakeup(
                                            MInstPtr inst,
                                            const WakeupEvent& event);
    void                                apply_vec_wakeup(
                                            MInstPtr inst,
                                            const WakeupEvent& event);
    void                                apply_tile_wakeup(
                                            MInstPtr inst,
                                            const WakeupEvent& event);
    void                                collect_candidates(std::vector<IssueRequest>& candidates);
    void                                collect_store_candidates(
                                            MInstPtr inst,
                                            std::vector<IssueRequest>& candidates);
    void                                select_candidates(
                                            const std::vector<IssueRequest>& candidates,
                                            size_t width,
                                            bool (*type_match)(InstType));
    bool                                request_ready(const IssueRequest& request) const;
    bool                                store_part_ready(MInstPtr inst, StorePart part) const;
    void                                mark_dispatched(const IssueRequest& request);
    void                                emit_request_reads(const IssueRequest& request);
    void                                clear_bypass_hint(const IssueRequest& request);
    void                                requeue_store(MInstPtr inst);
    void                                requeue_inst(MInstPtr inst);
    void                                EmitSrcRead(
                                            const MInstPtr inst,
                                            const Operand& src,
                                            int idx,
                                            FuType fu = FuType::NONE);
    void                                EmitVecSrcRead(const MInstPtr inst, const Operand& src, int idx);
    void                                EmitTileSrcRead(const MInstPtr inst, const Operand& src, int idx);
    FuType                              GetFuType(const MInstPtr inst);
    void                                update_next_ready(std::vector<MInstPtr>& inst_q);

private:
    std::vector<MInstPtr>               m_int_q;
    std::vector<MInstPtr>               m_ls_q;
    std::vector<MInstPtr>               m_fp_q;
    std::vector<MInstPtr>               m_vec_q;
    std::vector<MInstPtr>               m_tensor_q;
    ROB&                                m_rob;
    LSU&                                m_lsu;
    ExecuteUnit&                        m_exec;
    INPUT ASyncQueue<MInstPtr>&         m_rename_issue_q;
    INPUT FlushEvent&                   m_flush_event;
    OUTPUT ASyncQueue<RegReq>&          m_reg_req_q;
    OUTPUT ASyncQueue<VecRegReq>&       m_vec_reg_req_q;
    OUTPUT ASyncQueue<TileRegReq>&      m_tile_reg_req_q;
    INPUT ASyncQueue<FuAcceptEvent>&    m_fu_accept_q;
    std::vector<IssueRequest>           m_select_buffer;
    PerfStats*                          m_stats { nullptr };

    size_t                              m_int_issue_width  { 4 };
    size_t                              m_ls_issue_width   { 2 };
    size_t                              m_fp_issue_width   { 2 };
    size_t                              m_vec_issue_width  { 1 };
    size_t                              m_tile_issue_width { 1 };
    INPUT std::vector<WakeupEvent>&     m_wakeup_bus;
};

#endif
