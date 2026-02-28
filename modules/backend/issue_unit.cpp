
#include "issue_unit.h"

void IssueUnit::set_dispatch_issue_q(ASyncQueue<DynInst>* q) {
    m_dispatch_issue_q = q;
}

void IssueUnit::set_issue_execute_q(ASyncQueue<DynInst>* q) {
    m_issue_execute_q = q;
}

void IssueUnit::tick() {
    return;
}

void IssueUnit::commit() {
    return;
}
