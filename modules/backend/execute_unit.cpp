#include "execute_unit.h"

ExecuteUnit::ExecuteUnit(const std::string& n)
    : IExecute(n) { }

void ExecuteUnit::set_issue_execute_q(ASyncQueue<DynInst>* q) {
    m_issue_execute_q = q;
}

void ExecuteUnit::set_execute_commit_q(ASyncQueue<ExecResult>* q) {
    m_execute_commit_q = q;
}

void ExecuteUnit::tick() {
    return;
}

void ExecuteUnit::commit() {
    return;
}
