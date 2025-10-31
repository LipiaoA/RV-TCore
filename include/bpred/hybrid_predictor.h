#ifndef RV_TCORE_HYBRIDPREDICTOR_H
#define RV_TCORE_HYBRIDPREDICTOR_H

#include <bits/stdc++.h>
#include "global_predictor.h"

class HybridPredictor {
public:
    HybridPredictor(size_t bht_entries, unsigned pc_low_bits,
                    unsigned bhr_bits, unsigned ghr_bits,
                    bool chooser_as_table = false,
                    size_t chooser_entries = 1024);

    bool predict(uint64_t pc, bool &local_pred, bool &global_pred);
    void update(uint64_t pc, bool taken);

    double accuracy() const { return m_total ? (double)m_correct / m_total : 0.0; }
    double local_acc() const { return m_total ? (double)m_local_correct / m_total : 0.0; }
    double global_acc() const { return m_total ? (double)m_global_correct / m_total : 0.0; }

private:
    LocalPredictor m_local;
    GlobalPredictor m_global;
    unsigned m_pc_low_bits;

    bool m_chooser_as_table;
    size_t m_chooser_table_size = 0;
    vector<Sat2> m_chooser_table;
    SatN m_chooser_single{2};

    size_t m_total, m_correct, m_local_correct, m_global_correct, m_chooser_updates;

    bool choose(uint64_t pc) const;

    void adjust_chooser(uint64_t pc, bool toward_global);

    size_t chooser_index(uint64_t pc) const;
};

#endif
