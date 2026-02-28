
#ifndef RV_TCORE_COMMITUNIT_H
#define RV_TCORE_COMMITUNIT_H

#include "intf.h"
#include "module_comm.h"

class CommitUnit  : public ICommit {
public:
    CommitUnit(const std::string& n) : ICommit(n) {}
    ~CommitUnit() {}
public:
    void                                tick() {}
    void                                commit() {}
    void                                set_execute_commit_q(ASyncQueue<ExecResult>* q);

private:
    INPUT ASyncQueue<ExecResult>*       m_execute_commit_q { nullptr };
};

#endif