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

private:
    void                                async();
private:

    std::vector<MInstPtr>               m_int_q;
    std::vector<MInstPtr>               m_ls_q;
    ROB&                                m_rob;
    ExecuteUnit&                        m_exec;
    ASyncQueue<MInstPtr>&               m_rename_issue_q;
    ASyncQueue<MInstPtr>&               m_issue_execute_q;

    std::vector<MInstPtr>               m_select_buffer;

    size_t                              m_int_issue_width  { 4 };
    size_t                              m_ls_issue_width   { 2 };
    size_t                              m_fp_issue_width   { 2 };
};

#endif
