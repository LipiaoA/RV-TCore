
#ifndef RV_TCORE_ISSUEUNIT_H
#define RV_TCORE_ISSUEUNIT_H

#include "intf.h"
#include "module_comm.h"

class IssueUnit : public IIssue {
public:
    IssueUnit(const std::string& n) : IIssue(n) {}
    ~IssueUnit() {}
public:
    void                                set_dispatch_issue_q(ASyncQueue<DynInst>* q);
    void                                set_issue_execute_q(ASyncQueue<DynInst>* q);
    void                                tick() override;
    void                                commit() override;
private:
    INPUT  ASyncQueue<DynInst>*         m_dispatch_issue_q { nullptr };
    OUTPUT ASyncQueue<DynInst>*         m_issue_execute_q  { nullptr };
};

#endif