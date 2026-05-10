#include "issue_unit.h"
#include "isa/opcode.h"
#include <algorithm>
#include "execute/execute_unit.h"

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rename_issue_q(*deps.rename_issue_q),
      m_issue_execute_q(*deps.issue_execute_q),
      m_rob(*deps.rob),
      m_exec(*deps.exec),
      m_int_issue_width(conf.int_issue_width),
      m_ls_issue_width(conf.ls_issue_width),
      m_fp_issue_width(conf.fp_issue_width)
{}

void IssueUnit::build() {
}

void IssueUnit::tick() {
    async();

    issue();
    select();
}

void IssueUnit::commit() {}

void IssueUnit::wakeup() {

}

void IssueUnit::select()
{
    const size_t select_width = m_int_issue_width + m_ls_issue_width;
    if (select_width == 0 || (m_int_q.empty() && m_ls_q.empty())) {
        return;
    }

    std::vector<MInstPtr> candidates;
    candidates.reserve(m_int_q.size() + m_ls_q.size());

    for (const auto& inst : m_int_q) {
        if (inst->ready()) {
            candidates.emplace_back(inst);
        }
    }
    for (const auto& inst : m_ls_q) {
        if (inst->ready()) {
            candidates.emplace_back(inst);
        }
    }
    if (candidates.empty()) {
        return;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const MInstPtr lhs, const MInstPtr rhs) {
                         return m_rob.is_older_than(lhs->rob_id, rhs->rob_id);
                     });

    for (size_t i = 0; i < m_int_issue_width && i < candidates.size(); ++i) {
        auto &inst = candidates[i];
        if (inst->type == InstType::INT || inst->type == InstType::STD || inst->type == InstType::BRANCH) {
            m_select_buffer.emplace_back(inst);
        }
    }
    for (size_t i = 0; i < m_ls_issue_width && i < candidates.size(); ++i) {
        auto &inst = candidates[i];
        if (inst->type == InstType::LOAD || inst->type == InstType::STA) {
            m_select_buffer.emplace_back(inst);
        }
    }
}

void IssueUnit::issue()
{
    for (auto &inst : m_select_buffer) {
        bool ret = m_exec.dispatch(inst);
        LOG_LEVEL1("IssueUnit", "issued:%s %s, v%d-a%d-p%d%s%d", ret ? "true" : "false",
                   inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
                   inst->rd.ptag, inst->rob_id.idx == 0 ? "++" : "--", inst->rob_id.idx);
    }
    m_select_buffer.clear();
    for (auto it = m_int_q.begin(); it != m_int_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_int_q.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_ls_q.begin(); it != m_ls_q.end();) {
        if ((*it)->state == InstState::ISSUED) {
            it = m_ls_q.erase(it);
        } else {
            ++it;
        }
    }
    return;
}

void IssueUnit::async() {
    MInstPtr inst;
    while (m_rename_issue_q.read(inst)) {
        InstOp op = inst->opcode;
        if (is_load_store(op))
            m_ls_q.emplace_back(inst);
        else {
            m_int_q.emplace_back(inst);
        }
    }
    return;
}
