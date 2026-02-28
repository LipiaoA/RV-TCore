#include "commit_unit.h"

void CommitUnit::set_execute_commit_q(ASyncQueue<ExecResult>* q) {
    m_execute_commit_q = q;
}
