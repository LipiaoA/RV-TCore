#ifndef TCORE_LOCALPREDICTOR_H
#define TCORE_LOCALPREDICTOR_H
#include "intf.h"

class LocalPredictor {
public:
    LocalPredictor(size_t bht_entries, unsigned pc_low_bits, unsigned bhr_bits = 4)
        : m_bht(bht_entries, bhr_bits),
          m_pc_low_bits(pc_low_bits),
          m_bhr_bits(bhr_bits)
    {
        m_pht_bits = m_bhr_bits + m_pc_low_bits;
        m_pht_size = 1ull << m_pht_bits;
        m_pht.assign(m_pht_size, Sat2(2));
    }

    bool predict(uint64_t pc, int &out_index, uint32_t &out_bhr) {
        out_bhr = m_bht.get_bhr(pc);
        uint64_t pc_low = pc & ((1ull << m_pc_low_bits) - 1);
        uint64_t idx = ((uint64_t)pc_low << m_bhr_bits) | out_bhr;
        idx &= (m_pht_size - 1);
        out_index = (int)idx;
        return m_pht[idx].taken();
    }

    void update(uint64_t pc, bool taken) {
        uint32_t bhr = m_bht.get_bhr(pc);
        uint64_t pc_low = pc & ((1ull << m_pc_low_bits) - 1);
        uint64_t idx = ((uint64_t)pc_low << m_bhr_bits) | bhr;
        idx &= (m_pht_size - 1);
        m_pht[idx].update(taken);
        m_bht.update_bhr(pc, taken);
    }

    size_t bht_size() const { return m_bht.size(); }
    unsigned bhr_bits() const { return m_bhr_bits; }

private:
    BHT m_bht;
    unsigned m_pc_low_bits;
    unsigned m_bhr_bits;
    unsigned m_pht_bits;
    size_t m_pht_size;
    vector<Sat2> m_pht;
};
#endif
