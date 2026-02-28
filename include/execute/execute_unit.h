#ifndef RV_TCORE_EXECUTEUNIT_H
#define RV_TCORE_EXECUTEUNIT_H

#include "intf.h"
#include "module_comm.h"

class ExecuteUnit : public IExecute {
public:
    ExecuteUnit(const std::string& n);
    ~ExecuteUnit() {}
public:
    void                                set_issue_execute_q(ASyncQueue<DynInst>* q);
    void                                set_execute_commit_q(ASyncQueue<ExecResult>* q);
    void                                tick() override;
    void                                commit() override;
private:
    INPUT  ASyncQueue<DynInst>*         m_issue_execute_q  { nullptr };
    OUTPUT ASyncQueue<ExecResult>*      m_execute_commit_q { nullptr };
};

#endif