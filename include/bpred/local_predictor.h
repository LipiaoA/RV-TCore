#ifndef TCORE_LOCALPREDICTOR_H
#define TCORE_LOCALPREDICTOR_H

#include "intf.h"
#include "bht.h"

class LocalPredictor {
public:
    LocalPredictor(size_t bht_entries, unsigned pc_low_bits, unsigned bhr_bits = 4);

    bool                            predict(u64 pc, int &out_index, u32 &out_bhr);
    void                            update(u64 pc, bool taken);
    size_t                          bht_size() const { return m_bht.size(); }
    unsigned                        bhr_bits() const { return m_bhr_bits; }

private:
    BHT                             m_bht;
    unsigned                        m_pc_low_bits;
    unsigned                        m_bhr_bits;
    unsigned                        m_pht_bits;
    size_t                          m_pht_size;
    std::vector<Sat2> m_pht;
};

#endif
