
#ifndef TCORE_TARGETPREDICTOR_H
#define TCORE_TARGETPREDICTOR_H

// ----------------------------- BHT / BHR --------------------------------
class BHR {
public:
    BHR(int bits): m_bits(bits), m_val(0) {}
    void push(bool taken) { m_val = ((m_val<<1) | (taken?1:0)) & ((1ull<<m_bits)-1); }
    u64 value() const { return m_val; }
    void set(u64 v) { m_val = v & ((1ull<<m_bits)-1); }
private:
    int m_bits;
    u64 m_val;
};

// ----------------------------- TargetPredictor --------------------------------
enum InstType { DIRECT, INDIRECT, CALL, RET, OTHER };

struct InstRecord {
    u64 pc;
    InstType type;
    u64 actual_target; // for branches/calls/indirect/ret
    bool taken; // for direct conditional branch semantics
};

struct SimConfig {
    int direct_sets=128, direct_ways=4, direct_tagbits=16;
    int ras_depth=16, ras_cache_entries=64, ras_cache_tagbits=10;
    int itb_sets=256, itb_ways=4, itb_tagbits=14, itb_bhr_bits=12;
    int pc_lowdrop=2; // word aligned
};

struct Stats {
    uint64_t direct_hits=0, direct_misses=0;
    uint64_t indirect_hits=0, indirect_misses=0;
    uint64_t call_preds=0, ret_preds=0, ras_hits=0, ras_misses=0;
};

class TargetPredictor {
public:
    TargetPredictor(const SimConfig &cfg): cfg(cfg),
        direct(cfg.direct_sets, cfg.direct_ways, cfg.direct_tagbits, cfg.pc_lowdrop),
        ras(cfg.ras_depth), ras_cache(cfg.ras_cache_entries, cfg.ras_cache_tagbits),
        itb(cfg.itb_sets, cfg.itb_ways, cfg.itb_tagbits, cfg.itb_bhr_bits, cfg.pc_lowdrop),
        bhr(cfg.itb_bhr_bits) {}

    void run(const vector<InstRecord>& trace) {
        uint32_t time = 1;
        for (auto &inst : trace) {
            // FETCH: do prediction
            switch (inst.type) {
                case DIRECT: {
                    auto [hit, tgt] = direct.lookup(inst.pc, time);
                    if (hit) { stats.direct_hits++; /* assume predictor says taken when in BTB */ }
                    else { stats.direct_misses++; }
                    break;
                }
                case CALL: {
                    // push speculative return address (pc+4)
                    u64 ret_addr = inst.pc + 4;
                    ras.speculative_push(ret_addr);
                    stats.call_preds++;
                    break;
                }
                case RET: {
                    stats.ret_preds++;
                    u64 top = ras.peek_spec_top();
                    if (top!=0) { stats.ras_hits++; }
                    else { stats.ras_misses++; }
                    break;
                }
                case INDIRECT: {
                    auto [hit, tgt] = itb.lookup(inst.pc, bhr.value(), time);
                    if (hit) stats.indirect_hits++; else stats.indirect_misses++;
                    break;
                }
                default: break;
            }

            // COMMIT: update structures based on actual outcome
            switch (inst.type) {
                case DIRECT: {
                    if (inst.taken) direct.insert(inst.pc, inst.actual_target, time);
                    // update BHR for history (if we treat direct as branch)
                    bhr.push(inst.taken);
                    break;
                }
                case CALL: {
                    // commit push
                    ras.commit_push();
                    // also insert into direct BTB (calls are branches)
                    direct.insert(inst.pc, inst.actual_target, time);
                    break;
                }
                case RET: {
                    // commit pop
                    // compare predicted top with actual target; if mismatch, try ras_cache
                    u64 predicted = ras.peek_spec_top();
                    if (predicted && predicted==inst.actual_target) {
                        // good
                    } else if (!predicted) {
                        // try ras_cache using callsite tag? we don't have callsite here in this trace
                        // for simulation insert the mapping from a prior call if available
                    }
                    ras.commit_pop();
                    break;
                }
                case INDIRECT: {
                    bool correct = false; // whether a prior prediction matched actual target
                    // For simulation, use actual target to update ITB
                    itb.insert_or_update(inst.pc, bhr.value(), inst.actual_target, true, time);
                    // We also push history maybe based on whether it is considered taken
                    bhr.push(true);
                    break;
                }
                default: break;
            }

            ++time;
        }
    }

    void print_stats() const {
        cout << "===== Simulation Stats =====\n";
        cout << "Direct BTB hits: " << stats.direct_hits << " misses: " << stats.direct_misses << "\n";
        cout << "Indirect hits: " << stats.indirect_hits << " misses: " << stats.indirect_misses << "\n";
        cout << "Calls: " << stats.call_preds << " RAS hits: " << stats.ras_hits << " misses: " << stats.ras_misses << "\n";
    }

private:
    SimConfig cfg;
    DirectBTB direct;
    RAS ras;
    RASCache ras_cache;
    IndirectBTB itb;
    BHR bhr;
    Stats stats;
};


#endif
