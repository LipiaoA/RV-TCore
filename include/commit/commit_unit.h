
#ifndef RV_TCORE_COMMITUNIT_H
#define RV_TCORE_COMMITUNIT_H

#include "intf.h"
#include "module_comm.h"

class ROB;
class RenameUnit;

class CommitUnit : public ICommit {
public:
    CommitUnit(const CommitDeps& deps);
    ~CommitUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;

private:
    ASyncQueue<ExecResult>&             m_execute_commit_q;
    ROB&                                m_rob;
    RenameUnit&                         m_rename;
};

#endif
