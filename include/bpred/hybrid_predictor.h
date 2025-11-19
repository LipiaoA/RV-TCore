#ifndef RV_TCORE_HYBRIDPREDICTOR_H
#define RV_TCORE_HYBRIDPREDICTOR_H

#include <bits/stdc++.h>
#include "global_predictor.h"
#include "local_predictor.h"

class HybridPredictor {
public:
    HybridPredictor(size_t bht_entries, unsigned pc_low_bits,
                    unsigned bhr_bits, unsigned ghr_bits,
                    bool chooser_as_table = false,
                    size_t chooser_entries = 1024);
    ~HybridPredictor() {}

public:
    bool                                    predict(u64 pc);
    void                                    update(u64 pc, bool taken);

    double                                  accuracy() const;
    double                                  local_acc() const;
    double                                  global_acc() const;

private:
    bool                                    pred(u64 pc, bool &local_pred, bool &global_pred);
    bool                                    choose(u64 pc) const;
    void                                    adjust_chooser(u64 pc, bool toward_global);
    size_t                                  chooser_index(u64 pc) const;


private:
    LocalPredictor                          m_local;
    GlobalPredictor                         m_global;
    u32                                     m_pc_low_bits;

    bool                                    m_chooser_as_table;
    size_t                                  m_chooser_table_size = 0;
    std::vector<Sat2>                       m_chooser_table;
    SatN                                    m_chooser_single{2};

    /* stats */
    size_t                                  m_total;
    size_t                                  m_correct;
    size_t                                  m_local_correct;
    size_t                                  m_global_correct;
    size_t                                  m_chooser_updates;

};

#endif
