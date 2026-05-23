#include "execute_unit.h"

ExecuteUnit::ExecuteUnit(const ExecConfig& conf, const ExecDeps& deps)
    : IExecute(),
     m_inst_commit_q(*deps.inst_commit_q),
     m_flush_event(*deps.flush_event),
     m_lsu(*deps.lsu),
     m_rob(*deps.rob),
     m_alu_num(conf.alu_num),
     m_fpu_num(conf.fpu_num),
     m_bru_num(conf.bru_num),
     m_lsu_width(conf.agu_num),
     m_alu_conf(conf.alu_conf),
     m_fpu_conf(conf.fpu_conf),
     m_bru_conf(conf.bru_conf),
     m_lsu_conf(conf.lsu_conf)
{
    ALUDeps *alu_deps = deps.alu_deps;
    FPUDeps *fpu_deps = deps.fpu_deps;
    BRUDeps *bru_deps = deps.bru_deps;
    m_alus.init(m_alu_num, m_alu_conf, *alu_deps);
    m_fpus.init(m_fpu_num, m_fpu_conf, *fpu_deps);
    m_brus.init(m_bru_num, m_bru_conf, *bru_deps);
    m_lsu.build();
}

void ExecuteUnit::build() {
    return;
}

void ExecuteUnit::tick() {

    for (int i = 0; i < m_alu_num; i++) {
        m_alus[i].tick();
    }
    for (int i = 0; i < m_fpu_num; i++) {
        m_fpus[i].tick();
    }
    for (int i = 0; i < m_bru_num; i++) {
        m_brus[i].tick();
    }
    m_lsu.tick();
    arbitration();
    return;
}

void ExecuteUnit::commit() {
    for (int i = 0; i < m_alu_num; i++) {
        m_alus[i].commit();
    }
    for (int i = 0; i < m_fpu_num; i++) {
        m_fpus[i].commit();
    }
    for (int i = 0; i < m_bru_num; i++) {
        m_brus[i].commit();
    }
    m_lsu.commit();
    m_inst_commit_q.commit();
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

void ExecuteUnit::arbitration() {
    u32 committed = 0;

    auto try_commit = [&](auto& fu) {
        if (committed >= m_commit_width || !fu.commit_ready()) {
            return;
        }

        MInstPtr inst = fu.commit_inst();
        if (m_inst_commit_q.write(inst)) {
            fu.commit_ack();
            ++committed;
        }
        return;
    };

    for (u32 i = 0; i < m_alu_num && committed < m_commit_width; ++i) {
        try_commit(m_alus[i]);
    }
    for (u32 i = 0; i < m_bru_num && committed < m_commit_width; ++i) {
        try_commit(m_brus[i]);
    }
    // for (u32 i = 0; i < m_lsu_width && committed < m_commit_width; ++i) {
    //     try_commit(m_lsu);
    // }
}

void ExecuteUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    for (int i = 0; i < m_alu_num; i++) {
        m_alus[i].flush(m_flush_event);
    }
    for (int i = 0; i < m_fpu_num; i++) {
        m_fpus[i].flush(m_flush_event);
    }
    for (int i = 0; i < m_bru_num; i++) {
        m_brus[i].flush(m_flush_event);
    }
    m_lsu.flush(m_flush_event);
    m_inst_commit_q.flush_if([&](const MInstPtr& inst) {
        return m_rob.is_older_than(m_flush_event.rid, inst->rid);
    });
    return;
}
