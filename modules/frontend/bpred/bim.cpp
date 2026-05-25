#include "bim.h"

BIM::BIM(u32 entries, u32 histLen, u32 fetchWidth)
    : m_entries(entries),
      m_histLen(histLen),
      m_fetch_width(fetchWidth)
{
    std::fill(m_table, m_table + MAX_ENTRIES, SatCounter());
}

BIM::BIM(const BIMConfig& config)
    : m_entries(config.entries),
      m_histLen(config.hist_len),
      m_fetch_width(config.fetch_width)
{
    std::fill(m_table, m_table + MAX_ENTRIES, SatCounter());
}

branch_pred BIM::predict(pc_t pc)
{
    branch_pred predict;
    predict.ghr_snapshot = m_ghr;

    u64 spec_ghr = m_ghr;

    for (u32 i = 0; i < m_fetch_width; i++) {
        pc_t pc = pc + i * 4;

        u32 idx = ((pc >> 2) ^ spec_ghr) % m_entries;
        bool taken = m_table[idx].predict();

        predict.pred[i].valid = true;
        predict.pred[i].taken = taken;
        predict.pred[i].target = 0ull;

        spec_ghr = ((spec_ghr << 1) | taken) & ((1 << m_histLen) - 1);
    }
    m_ghr = spec_ghr;
    return predict;
}

void BIM::update(pc_t pc, bool taken)
{
    u32 idx = index(pc);
    m_table[idx].update(taken);
    return;
}

u32 BIM::index(pc_t pc) const
{
    return ((pc >> 2) ^ m_ghr) % m_entries;
}

void BIM::rollback(u64 ghr)
{
    m_ghr = ghr;
}