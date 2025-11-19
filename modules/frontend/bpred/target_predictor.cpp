#include "target_predictor.h"

TargetPredictor::TargetPredictor(const SimConfig &cfg): m_cfg(cfg),
    m_direct(cfg.direct_sets, cfg.direct_ways, cfg.direct_tagbits, cfg.pc_lowdrop),
    m_ras(cfg.ras_depth), m_ras_cache(cfg.ras_cache_entries, cfg.ras_cache_tagbits),
    m_itb(cfg.itb_sets, cfg.itb_ways, cfg.itb_tagbits, cfg.itb_bhr_bits, cfg.pc_lowdrop),
    m_bhr(cfg.itb_bhr_bits) {}

void TargetPredictor::run(const std::vector<InstRecord>& trace) {
    uint32_t time = 1;
    for (auto &inst : trace) {
        // FETCH: do prediction
        switch (inst.type) {
            case DIRECT: {
                auto [hit, tgt] = m_direct.lookup(inst.pc, time);
                if (hit) { m_stats.direct_hits++; /* assume predictor says taken when in BTB */ }
                else { m_stats.direct_misses++; }
                break;
            }
            case CALL: {
                // push speculative return address (pc+4)
                u64 ret_addr = inst.pc + 4;
                m_ras.speculative_push(ret_addr);
                m_stats.call_preds++;
                break;
            }
            case RET: {
                m_stats.ret_preds++;
                u64 top = m_ras.peek_spec_top();
                if (top!=0) { m_stats.ras_hits++; }
                else { m_stats.ras_misses++; }
                break;
            }
            case INDIRECT: {
                auto [hit, tgt] = m_itb.lookup(inst.pc, m_bhr.value(), time);
                if (hit) m_stats.indirect_hits++;
                else m_stats.indirect_misses++;
                break;
            }
            default: break;
        }

        // COMMIT: update structures based on actual outcome
        switch (inst.type) {
            case DIRECT: {
                if (inst.taken) m_direct.insert(inst.pc, inst.actual_target, time);
                // update BHR for history (if we treat m_direct as branch)
                m_bhr.push(inst.taken);
                break;
            }
            case CALL: {
                // commit push
                m_ras.commit_push();
                // also insert into m_direct BTB (calls are branches)
                m_direct.insert(inst.pc, inst.actual_target, time);
                break;
            }
            case RET: {
                // commit pop
                // compare predicted top with actual target; if mismatch, try ras_cache
                u64 predicted = m_ras.peek_spec_top();
                if (predicted && predicted==inst.actual_target) {
                    // good
                } else if (!predicted) {
                    // try ras_cache using callsite tag? we don't have callsite here in this trace
                    // for simulation insert the mapping from a prior call if available
                }
                m_ras.commit_pop();
                break;
            }
            case INDIRECT: {
                bool correct = false; // whether a prior prediction matched actual target
                // For simulation, use actual target to update ITB
                m_itb.insert_or_update(inst.pc, m_bhr.value(), inst.actual_target, true, time);
                // We also push history maybe based on whether it is considered taken
                m_bhr.push(true);
                break;
            }
            default: break;
        }

        ++time;
    }
}

void TargetPredictor::print_stats() const {
    std::cout << "===== Simulation Stats =====\n";
    std::cout << "Direct BTB hits: " << m_stats.direct_hits << " misses: " << m_stats.direct_misses << "\n";
    std::cout << "Indirect hits: " << m_stats.indirect_hits << " misses: " << m_stats.indirect_misses << "\n";
    std::cout << "Calls: " << m_stats.call_preds << " RAS hits: " << m_stats.ras_hits << " misses: " << m_stats.ras_misses << "\n";
}
