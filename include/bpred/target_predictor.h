
#ifndef TCORE_TARGETPREDICTOR_H
#define TCORE_TARGETPREDICTOR_H

#include "util.h"
#include "direct_btb.h"
#include "ras.h"
#include "ras_cache.h"
#include "indirect_btb.h"
#include "bhr.h"

enum InstType { DIRECT, INDIRECT, CALL, RET, OTHER };

struct InstRecord {
    u64 pc;
    InstType type;
    u64 actual_target; // for branches/calls/indirect/ret
    bool taken; // for direct conditional branch semantics
};

struct SimConfig {
    int direct_sets = 128, direct_ways = 4, direct_tagbits = 16;
    int ras_depth = 16, ras_cache_entries = 64, ras_cache_tagbits = 10;
    int itb_sets = 256, itb_ways = 4, itb_tagbits = 14, itb_bhr_bits = 12;
    int pc_lowdrop = 2; // word aligned
};

struct Stats {
    u64 direct_hits = 0, direct_misses = 0;
    u64 indirect_hits = 0, indirect_misses = 0;
    u64 call_preds = 0, ret_preds = 0, ras_hits = 0, ras_misses = 0;
};

class TargetPredictor {
public:
    TargetPredictor(const SimConfig &cfg);
    ~TargetPredictor() {}

public:
    void                                        run(const std::vector<InstRecord>& trace);
    void                                        print_stats() const;

private:
    SimConfig                                   m_cfg;
    DirectBTB                                   m_direct;
    RAS                                         m_ras;
    RASCache                                    m_ras_cache;
    IndirectBTB                                 m_itb;
    BHR                                         m_bhr;
    Stats                                       m_stats;
};

#endif
