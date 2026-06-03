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
    void                                EmitSrcRead(const MInstPtr inst, const RegType& src, int idx);
    FuType                              GetFuType(const MInstPtr inst);
    bool                                is_buffered(const MInstPtr& inst) const;
    size_t                              count_buffered_int_slots() const;
    size_t                              count_buffered_ls_slots() const;

private:
    std::vector<MInstPtr>               m_int_q;
    std::vector<MInstPtr>               m_ls_q;
    std::vector<MInstPtr>               m_fp_q;
    ROB&                                m_rob;
    ExecuteUnit&                        m_exec;
    INPUT ASyncQueue<MInstPtr>&         m_rename_issue_q;
    INPUT FlushEvent&                   m_flush_event;
    OUTPUT ASyncQueue<RegReq>&          m_reg_req_q;
    std::vector<MInstPtr>               m_select_buffer;
    PerfStats*                          m_stats { nullptr };

    size_t                              m_int_issue_width  { 4 };
    size_t                              m_ls_issue_width   { 2 };
    size_t                              m_fp_issue_width   { 2 };
    INPUT std::vector<WakeupEvent>&     m_wakeup_bus;
};

#endif
