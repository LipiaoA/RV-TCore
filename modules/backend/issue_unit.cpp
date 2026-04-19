#include "issue_unit.h"
#include "isa/opcode.h"

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rename_issue_q(*deps.rename_issue_q),
      m_issue_execute_q(*deps.issue_execute_q),
      m_int_issue_width(conf.int_issue_width),
      m_ls_issue_width(conf.ls_issue_width),
      m_fp_issue_width(conf.fp_issue_width)
{}

void IssueUnit::build() {
}

bool IssueUnit::is_load_store(InstOp op) {
    return (op >= OP_LB && op <= OP_LWU) || (op >= OP_SB && op <= OP_SD);
}

bool IssueUnit::is_fp(InstOp op) {
    (void)op;
    return false;
}

void IssueUnit::tick() {
    MicroOp inst;
    while (m_rename_issue_q.read(inst)) {
        InstOp op = inst.opcode;
        if (is_load_store(op))
            m_ls_q.push_back(std::move(inst));
        else if (is_fp(op))
            m_fp_q.push_back(std::move(inst));
        else
            m_int_q.push_back(std::move(inst));
    }

    size_t int_issued = 0, ls_issued = 0, fp_issued = 0;
    auto try_issue = [this](std::deque<MicroOp>& q, size_t& issued, size_t width) {
        for (auto it = q.begin(); it != q.end() && issued < width; ) {
            if (it->rs1.ready && it->rs2.ready && !it->issued) {
                it->issued = true;
                // TODO: dispatch to issue_execute_q
                assert(false);
                it = q.erase(it);
                ++issued;
            } else
                ++it;
        }
    };
    try_issue(m_int_q, int_issued, m_int_issue_width);
    try_issue(m_ls_q,  ls_issued,  m_ls_issue_width);
    try_issue(m_fp_q,  fp_issued,  m_fp_issue_width);
}

void IssueUnit::commit() {}

void IssueUnit::wakeup() {}

void IssueUnit::selsect() {}
