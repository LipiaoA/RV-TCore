#ifndef RV_TCORE_ISSUEUNIT_H
#define RV_TCORE_ISSUEUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "decode/inst.h"
#include <deque>

class IssueUnit : public IIssue {
public:
    IssueUnit(const IssueConfig& conf, const IssueDeps& deps);
    ~IssueUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    void                                wakeup();
    void                                selsect();

    static bool                         is_load_store(InstOp op);
    static bool                         is_fp(InstOp op);

private:
    ASyncQueue<MicroOp>&                m_dispatch_issue_q;
    ASyncQueue<MicroOp>&                m_issue_execute_q;

    std::deque<MicroOp>                 m_int_q;
    std::deque<MicroOp>                 m_ls_q;
    std::deque<MicroOp>                 m_fp_q;

    size_t                              m_int_issue_width  { 4 };
    size_t                              m_ls_issue_width   { 2 };
    size_t                              m_fp_issue_width   { 2 };
};

#endif
