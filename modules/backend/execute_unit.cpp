#include "execute_unit.h"
#include "perf/perf_stats.h"

#include <algorithm>
#include <functional>
#include <vector>

ExecuteUnit::ExecuteUnit(const ExecConfig& conf, const ExecDeps& deps)
    : IExecute(),
     m_lsu(*deps.lsu),
     m_alu_conf(conf.alu_conf),
     m_fpu_conf(conf.fpu_conf),
     m_bru_conf(conf.bru_conf),
     m_agu_conf(conf.agu_conf),
     m_std_conf(conf.std_conf),
     m_alu_num(conf.alu_num),
     m_fpu_num(conf.fpu_num),
     m_vpu_num(conf.vpu_num),
     m_tpu_num(conf.tpu_num),
     m_bru_num(conf.bru_num),
     m_agu_num(conf.agu_num),
     m_std_num(conf.std_num),
     m_commit_width(conf.commit_width),
     m_inst_commit_q(*deps.inst_commit_q),
     m_flush_event(*deps.flush_event),
     m_rob(*deps.rob),
     m_reg_bypass_bus(*deps.alu_deps->reg_bypass_bus),
     m_stats(deps.perf_stats)
{
    ALUDeps *alu_deps = deps.alu_deps;
    FPUDeps *fpu_deps = deps.fpu_deps;
    VectorDeps *vector_deps = deps.vector_deps;
    TileDeps *tile_deps = deps.tile_deps;
    BRUDeps *bru_deps = deps.bru_deps;
    AGUDeps *agu_deps = deps.agu_deps;
    STDDeps *std_deps = deps.std_deps;
    if (m_alu_num > 0) {
        assert(alu_deps);
        m_alus.init(m_alu_num, m_alu_conf, *alu_deps);
    }
    if (m_fpu_num > 0) {
        assert(fpu_deps);
        m_fpus.init(m_fpu_num, m_fpu_conf, *fpu_deps);
    }
    if (m_vpu_num > 0) {
        assert(vector_deps);
        m_vpus.init(m_vpu_num, conf.vector_conf, *vector_deps);
    }
    if (m_tpu_num > 0) {
        assert(tile_deps);
        m_tpus.init(m_tpu_num, conf.tile_conf, *tile_deps);
    }
    if (m_bru_num > 0) {
        assert(bru_deps);
        m_brus.init(m_bru_num, m_bru_conf, *bru_deps);
    }
    if (m_agu_num > 0) {
        assert(agu_deps);
        m_agus.init(m_agu_num, m_agu_conf, *agu_deps);
    }
    if (m_std_num > 0) {
        assert(std_deps);
        m_stds.init(m_std_num, m_std_conf, *std_deps);
    }
    m_lsu.build();
}

void ExecuteUnit::build() {

}

void ExecuteUnit::tick() {
    flush();

    for (u32 i = 0; i < m_alu_num; i++) {
        m_alus[i].tick();
    }
    for (u32 i = 0; i < m_fpu_num; i++) {
        m_fpus[i].tick();
    }
    for (u32 i = 0; i < m_vpu_num; i++) {
        m_vpus[i].tick();
    }
    for (u32 i = 0; i < m_tpu_num; i++) {
        m_tpus[i].tick();
    }
    for (u32 i = 0; i < m_bru_num; i++) {
        m_brus[i].tick();
    }
    m_lsu.tick();
    for (u32 i = 0; i < m_agu_num; i++) {
        m_agus[i].tick();
    }
    for (u32 i = 0; i < m_std_num; i++) {
        m_stds[i].tick();
    }
    arbitration();
}

void ExecuteUnit::commit()
{
    for (u32 i = 0; i < m_alu_num; ++i) {
        m_alus[i].fwd_bypass();
    }
    for (u32 i = 0; i < m_bru_num; ++i) {
        m_brus[i].fwd_bypass();
    }
    for (u32 i = 0; i < m_agu_num; ++i) {
        m_agus[i].fwd_bypass();
    }
    for (u32 i = 0; i < m_std_num; ++i) {
        m_stds[i].fwd_bypass();
    }

    for (u32 i = 0; i < m_alu_num; i++) {
        m_alus[i].commit();
    }
    for (u32 i = 0; i < m_fpu_num; i++) {
        m_fpus[i].commit();
    }
    for (u32 i = 0; i < m_vpu_num; i++) {
        m_vpus[i].commit();
    }
    for (u32 i = 0; i < m_tpu_num; i++) {
        m_tpus[i].commit();
    }
    for (u32 i = 0; i < m_bru_num; i++) {
        m_brus[i].commit();
    }
    for (u32 i = 0; i < m_agu_num; i++) {
        m_agus[i].commit();
    }
    for (u32 i = 0; i < m_std_num; i++) {
        m_stds[i].commit();
    }
    m_lsu.commit();
    m_inst_commit_q.commit();
    m_reg_bypass_bus.clear();
}

bool ExecuteUnit::dispatch(MInstPtr inst)
{
    return dispatch({inst, StorePart::NONE, 0});
}

bool ExecuteUnit::dispatch(const IssueRequest& request)
{
    if (!request.inst) {
        return false;
    }
    bool accepted = request.part == StorePart::NONE
        ? dispatch_inst(request.inst)
        : dispatch_store(request);
    if (m_stats) {
        if (accepted) {
            ++m_stats->execute.dispatch_accepted;
        } else {
            ++m_stats->execute.dispatch_rejected;
        }
    }
    return accepted;
}

bool ExecuteUnit::dispatch_store(const IssueRequest& request)
{
    auto dispatch_request = [&](auto& fus, u32 count, u32& next) {
        for (u32 n = 0; n < count; ++n) {
            u32 idx = (next + n) % count;
            if (fus[idx].dispatch(request)) {
                next = (idx + 1) % count;
                return true;
            }
        }
        return false;
    };
    if (request.part == StorePart::STA) {
        return dispatch_request(m_agus, m_agu_num, m_next_agu);
    }
    if (request.part == StorePart::STD) {
        return dispatch_request(m_stds, m_std_num, m_next_std);
    }
    return false;
}

bool ExecuteUnit::dispatch_inst(MInstPtr inst)
{
    auto dispatch_fu = [&](auto& fus, u32 count, u32& next) {
        for (u32 n = 0; n < count; ++n) {
            u32 idx = (next + n) % count;
            if (fus[idx].dispatch(inst)) {
                next = (idx + 1) % count;
                return true;
            }
        }
        return false;
    };
    switch (inst->type) {
    case InstType::INT:
        if (dispatch_fu(m_alus, m_alu_num, m_next_alu)) {
            return true;
        }
        break;
    case InstType::BRANCH:
        if (dispatch_fu(m_brus, m_bru_num, m_next_bru)) {
            return true;
        }
        break;
    case InstType::FP:
        if (dispatch_fu(m_fpus, m_fpu_num, m_next_fpu)) {
            return true;
        }
        break;
    case InstType::VECTOR:
        if (dispatch_fu(m_vpus, m_vpu_num, m_next_vpu)) {
            return true;
        }
        break;
    case InstType::TENSOR:
        if (dispatch_fu(m_tpus, m_tpu_num, m_next_tpu)) {
            return true;
        }
        break;
    case InstType::STA:
    case InstType::STORE:
    case InstType::LOAD:
        if (dispatch_fu(m_agus, m_agu_num, m_next_agu)) {
            return true;
        }
        break;
    case InstType::STD:
        if (dispatch_fu(m_stds, m_std_num, m_next_std)) {
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void ExecuteUnit::arbitration() {
    struct Candidate {
        MInstPtr inst { nullptr };
        std::function<void()> ack;
    };
    std::vector<Candidate> ready;

    auto collect = [&](auto& fu) {
        if (fu.commit_ready()) {
            auto* fu_ptr = &fu;
            MInstPtr inst = fu.commit_inst();
            ready.push_back({inst, [fu_ptr]() { fu_ptr->commit_ack(); }});
        }
    };

    for (u32 i = 0; i < m_alu_num; ++i) {
        collect(m_alus[i]);
    }
    for (u32 i = 0; i < m_fpu_num; ++i) {
        collect(m_fpus[i]);
    }
    for (u32 i = 0; i < m_vpu_num; ++i) {
        collect(m_vpus[i]);
    }
    for (u32 i = 0; i < m_tpu_num; ++i) {
        collect(m_tpus[i]);
    }
    for (u32 i = 0; i < m_bru_num; ++i) {
        collect(m_brus[i]);
    }

    collect(m_lsu);

    std::stable_sort(ready.begin(), ready.end(),
                     [this](const Candidate& lhs, const Candidate& rhs) {
                         return m_rob.is_older_than(lhs.inst->rid, rhs.inst->rid);
                     });

    u32 committed = 0;
    for (auto& cand : ready) {
        if (committed >= m_commit_width) {
            break;
        }
        if (cand.inst && m_inst_commit_q.write(cand.inst)) {
            cand.inst->state = InstState::ISSUED;
            cand.ack();
            if (m_stats) {
                ++m_stats->execute.completed_insts;
                ++m_stats->execute.writeback_insts;
            }
            ++committed;
        }
    }
}

void ExecuteUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    for (u32 i = 0; i < m_alu_num; i++) {
        m_alus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_fpu_num; i++) {
        m_fpus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_vpu_num; i++) {
        m_vpus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_tpu_num; i++) {
        m_tpus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_bru_num; i++) {
        m_brus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_agu_num; i++) {
        m_agus[i].flush(m_flush_event);
    }
    for (u32 i = 0; i < m_std_num; i++) {
        m_stds[i].flush(m_flush_event);
    }
    m_lsu.flush(m_flush_event);
    m_inst_commit_q.flush_if([&](const MInstPtr& inst) {
        bool flushed = m_flush_event.scope == FlushScope::ALL ||
                       m_rob.is_older_than(m_flush_event.rid, inst->rid);
        if (flushed && m_stats) {
            ++m_stats->execute.flushed_insts;
        }
        return flushed;
    });
}

void ExecuteUnit::reset() {
    for (u32 i = 0; i < m_alu_num; i++) {
        m_alus[i].reset();
    }
    for (u32 i = 0; i < m_fpu_num; i++) {
        m_fpus[i].reset();
    }
    for (u32 i = 0; i < m_vpu_num; i++) {
        m_vpus[i].reset();
    }
    for (u32 i = 0; i < m_tpu_num; i++) {
        m_tpus[i].reset();
    }
    for (u32 i = 0; i < m_bru_num; i++) {
        m_brus[i].reset();
    }
    for (u32 i = 0; i < m_agu_num; i++) {
        m_agus[i].reset();
    }
    for (u32 i = 0; i < m_std_num; i++) {
        m_stds[i].reset();
    }
    m_lsu.reset();
    m_inst_commit_q.clear();
    m_next_alu = 0;
    m_next_fpu = 0;
    m_next_vpu = 0;
    m_next_tpu = 0;
    m_next_bru = 0;
    m_next_agu = 0;
    m_next_std = 0;
}
