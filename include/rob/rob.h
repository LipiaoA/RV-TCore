
#ifndef RV_TCORE_ROB_H
#define RV_TCORE_ROB_H

#include "intf.h"
#include "module_comm.h"

class ROB : public IRob {
public:
    ROB(const std::string& n) : IRob(n) {}
    ~ROB() {}
public:
    void                                set_execute_commit_q(ASyncQueue<ExecResult>* q);
    void                                tick() {}
    void                                commit() {}
private:
    INPUT ASyncQueue<ExecResult>*       m_execute_commit_q { nullptr };
};

#endif