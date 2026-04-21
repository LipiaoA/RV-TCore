
#include "local_predictor.h"

LocalPredictor::LocalPredictor(size_t bht_entries, unsigned pc_low_bits, unsigned bhr_bits)
    : m_bht(bht_entries, bhr_bits),
      m_pc_low_bits(pc_low_bits),
      m_bhr_bits(bhr_bits)
{
    m_pht_bits = m_bhr_bits + m_pc_low_bits;
    m_pht_size = 1ull << m_pht_bits;
    m_pht.assign(m_pht_size, Sat2(1));
}

bool LocalPredictor::predict(u64 pc, int &out_index, u32 &out_bhr) {
    out_bhr = m_bht.get_bhr(pc);
    u64 pc_low = pc & ((1ull << m_pc_low_bits) - 1);
    u64 idx = ((u64)pc_low << m_bhr_bits) | out_bhr;
    idx &= (m_pht_size - 1);
    out_index = (int)idx;
    return m_pht[idx].taken();
}

void LocalPredictor::update(u64 pc, bool taken) {
    u32 bhr = m_bht.get_bhr(pc);
    u64 pc_low = pc & ((1ull << m_pc_low_bits) - 1);
    u64 idx = ((u64)pc_low << m_bhr_bits) | bhr;
    idx &= (m_pht_size - 1);
    m_pht[idx].update(taken);
    m_bht.update_bhr(pc, taken);
}
