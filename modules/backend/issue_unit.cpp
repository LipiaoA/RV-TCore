#include "issue_unit.h"
#include "isa/opcode.h"
#include <algorithm>
#include "execute/execute_unit.h"

IssueUnit::IssueUnit(const IssueConfig& conf, const IssueDeps& deps)
    : IIssue("issue_unit"),
      m_rob(*deps.rob),
      m_exec(*deps.exec),
      m_rename_issue_q(*deps.rename_issue_q),
      m_flush_event(*deps.flush_event),
      m_reg_req_q(*deps.reg_req_q),
      m_int_issue_width(conf.int_issue_width),
      m_ls_issue_width(conf.ls_issue_width),
      m_fp_issue_width(conf.fp_issue_width),
      m_wakeup_bus(*deps.wakeup_bus)
{}

void IssueUnit::build() {
}

void IssueUnit::tick() {
    flush();
    /* run at previous clock cycle */
    async();
    wakeup();

    /* run at current clock cycle */
    issue();
    select();
}

void IssueUnit::commit()
{
    return;
}

void IssueUnit::wakeup() {
    auto apply = [](const MInstPtr inst, const WakeupEvent& event) {
        if (!event.valid ||
            (event.type != WAKEUP_BYPASS && event.type != WAKEUP_WRITEBACK)) {
            return;
        }
        if (inst->rs1.valid && inst->rs1.ptag == event.ptag) {
            inst->rs1.ready = true;
        }
        if (inst->rs2.valid && inst->rs2.ptag == event.ptag) {
            inst->rs2.ready = true;
        }
    };

    for (const auto& event : m_wakeup_bus) {
        for (auto inst : m_int_q) {
            apply(inst, event);
        }
        for (auto inst : m_ls_q) {
            apply(inst, event);
        }
        for (auto inst : m_fp_q) {
            apply(inst, event);
        }
    }
    m_wakeup_bus.clear();
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
                         return m_rob.is_older_than(lhs->rid, rhs->rid);
                     });
    u32 sel = 0;
    for (size_t i = 0; sel < m_int_issue_width && i < candidates.size(); ++i) {
        auto &inst = candidates[i];
        if (inst->type == InstType::INT || inst->type == InstType::STD || inst->type == InstType::BRANCH) {
            m_select_buffer.emplace_back(inst);
            EmitSrcRead(inst, inst->rs1, 1);
            EmitSrcRead(inst, inst->rs2, 2);
            ++sel;
        }
    }
    sel = 0;
    for (size_t i = 0; sel < m_ls_issue_width && i < candidates.size(); ++i) {
        auto &inst = candidates[i];
        if (inst->type == InstType::LOAD || inst->type == InstType::STA) {
            m_select_buffer.emplace_back(inst);
            EmitSrcRead(inst, inst->rs1, 1);
            EmitSrcRead(inst, inst->rs2, 2);
            ++sel;
        }
    }
}

void IssueUnit::issue()
{
    for (auto &inst : m_select_buffer) {
        bool ret = m_exec.dispatch(inst);
        LOG_LEVEL1("IssueUnit", "issued:%s pc:0x%lx, %s, v%d-x%d-p%d%s%d", ret ? "true" : "false", inst->pc,
                   inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
                   inst->rd.ptag, inst->rid.idx == 0 ? "++" : "--", inst->rid.idx);
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
        if (!inst->valid) {
            delete inst;
            continue;
        }
        InstOp op = inst->opcode;
        if (is_load_store(op))
            m_ls_q.emplace_back(inst);
        else {
            m_int_q.emplace_back(inst);
        }
    }
    return;
}

void IssueUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    auto clear_q = [&](std::vector<MInstPtr>& q) {
        std::vector<MInstPtr> tmp_q;
        for (auto inst : q) {
            if (m_rob.is_older_than(inst->rid, m_flush_event.rid)) {
                tmp_q.emplace_back(inst);
            } else {
                inst->valid = false;
            }
        }
        q.swap(tmp_q);
        return;
    };

    switch (m_flush_event.type) {
    case FlushType::BRANCH_MISPRED: {
        clear_q(m_int_q);
        clear_q(m_ls_q);
        clear_q(m_select_buffer);
        clear_q(m_fp_q);
        m_rename_issue_q.flush_if([&](const MInstPtr& inst) {
            return m_rob.is_older_than(m_flush_event.rid, inst->rid);
        });
    } break;
    case FlushType::EXCEPTION:
    case FlushType::INTERRUPT:
    case FlushType::DEBUG:
    case FlushType::MEMORY_ORDER:
    case FlushType::LOAD_REPLAY:
    case FlushType::FENCE: {
        assert(0);
    } break;
    default:
        assert(0);
    }
    return;
}

FuType IssueUnit::GetFuType(const MInstPtr inst)
{
    switch (inst->type) {
    case InstType::INT:
    case InstType::STD:
        return FuType::ALU;
    case InstType::BRANCH:
        return FuType::BRU;
    case InstType::LOAD:
    case InstType::STA:
    case InstType::STORE:
        return FuType::LSU;
    case InstType::FP:
        return FuType::FPU;
    default:
        return FuType::NONE;
    }
}

void IssueUnit::EmitSrcRead(const MInstPtr inst, const RegType& src, int idx)
{
    if (!src.valid) {
        return;
    }
    FuType fu = GetFuType(inst);
    if (fu == FuType::NONE) {
        assert(false);
    }
    RegReq req;
    req.valid = true;
    req.write = false;
    req.idx = idx;
    req.ptag = src.ptag;
    req.atag = src.atag;
    req.rid = inst->rid;
    req.fu = fu;
    m_reg_req_q.write(req);
}
