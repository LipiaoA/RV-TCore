#include "execute_unit.h"

ExecuteUnit::ExecuteUnit(const ExecConfig& conf, const ExecDeps& deps)
    : IExecute()
    , m_issue_execute_q(*deps.issue_execute_q)
    , m_execute_commit_q(*deps.execute_commit_q)
    , m_lsu(*deps.lsu)
    , m_alu_num(conf.alu_num)
    , m_fpu_num(conf.fpu_num)
    , m_bru_num(conf.bru_num)
    , m_lsu_width(conf.agu_num)
{
    ALUConfig alu_conf;
    FPUConfig fpu_conf;
    BRUConfig bru_conf;
    LSUConfig lsu_conf;

    m_alus.init(m_alu_num, alu_conf);
    m_fpus.init(m_fpu_num, fpu_conf);
    m_brus.init(m_bru_num, bru_conf);
    m_lsu.build();
}

void ExecuteUnit::build() {}

void ExecuteUnit::tick() {
    return;
}

void ExecuteUnit::commit() {
    return;
}

bool ExecuteUnit::dispatch(MInstPtr inst) {
    if (!inst)
        return false;
    switch (inst->type) {
    case InstType::INT:
    case InstType::STD:
    for (int i = 0; i < m_alu_num; i++) {
        if (m_alus[i].dispatch(inst)) {
            inst->state = InstState::ISSUED;
            return true;
        }
    } break;
    case InstType::BRANCH:
        for (int i = 0; i < m_bru_num; i++) {
            if (m_brus[i].dispatch(inst)) {
                inst->state = InstState::ISSUED;
                return true;
            }
        } break;
    case InstType::STA:
    case InstType::LOAD:
        for (int i = 0; i < m_lsu_width; i++) {
            if (m_lsu.dispatch(inst)) {
                inst->state = InstState::ISSUED;
                return true;
            }
        } break;
    default:
        assert(0);
    }
    return false;
}
