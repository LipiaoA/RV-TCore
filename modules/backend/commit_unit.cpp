#include "commit_unit.h"
#include "rob/rob.h"
#include "rename/rename_unit.h"

CommitUnit::CommitUnit(const CommitConfig& conf, const CommitDeps& deps)
    : ICommit()
    , m_inst_commit_q(*deps.inst_commit_q)
    , m_rob(*deps.rob)
    , m_reg_req_q(*deps.reg_req_q)
    , m_rename(*deps.rename)
    , m_flush_req_q(*deps.flush_req_q)
    , m_flush_event(*deps.flush_event)
    , m_commit_width(conf.commit_width)
    , m_resolve_width(conf.resolve_width)
{}

void CommitUnit::build() {}

void CommitUnit::tick() {
    inst_commit();
    inst_reslove();
}

void CommitUnit::inst_reslove() {
    if (m_inst_commit_q.empty()) {
        return;
    }
    for (u32 i = 0; i < m_resolve_width; i++) {
        MInstPtr inst;
        bool ret = m_inst_commit_q.read(inst);
        if (!ret || !inst) {
            continue;
        }
        if (inst->state == InstState::ISSUED) {
            inst->state = InstState::EXECUTED;
            LOG_LEVEL1("CommitUnit", "pc: 0x%lx, inst: %s resolved", inst->pc, inst->inst_name.c_str());
            write_back(inst);
        }
    }
}

void CommitUnit::inst_commit() {
    if (m_rob.empty()) {
        return;
    }
    for (u32 i = 0; i < m_commit_width; i++) {
        auto entry = m_rob.head();
        if (entry && entry->inst->state == InstState::EXECUTED) {
            entry->inst->state = InstState::RETIRED;
            if (!m_rob.commit_head()) {
                break;
            }
            LOG_LEVEL1("CommitUnit", "pc: 0x%lx commit inst: %s retired", entry->inst->pc, entry->inst->inst_name.c_str());
        } else {
            break;
        }
    }
    return;
}

void CommitUnit::write_back(const MInstPtr inst)
{
    if (!inst->rd.valid) {
        return;
    }
    RegReq req;
    req.valid = true;
    req.ptag = inst->rd.ptag;
    req.write = true;
    req.val = inst->rd.val;
    req.rid = inst->rid;
    m_reg_req_q.write(req);
}

void CommitUnit::commit()
{

}
