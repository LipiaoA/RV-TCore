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

u64 BIM::hist_mask() const
{
    if (m_histLen == 0 || m_histLen >= 64) {
        return m_histLen == 0 ? 0ull : ~0ull;
    }
    return (1ull << m_histLen) - 1ull;
}

u32 BIM::index(pc_t pc, u64 ghr) const
{
    if (m_entries == 0) {
        return 0;
    }
    return static_cast<u32>(((pc >> 2) ^ ghr) % m_entries);
}

u64 BIM::advance(u64 ghr, bool taken) const
{
    return ((ghr << 1) | (taken ? 1ull : 0ull)) & hist_mask();
}

branch_pred BIM::predict(pc_t base, u32 nslots, u32 cond_mask)
{
    branch_pred predict;
    predict.ghr_snapshot = m_ghr;
    if (nslots > MAX_PREDICT_WIDTH) {
        nslots = MAX_PREDICT_WIDTH;
    }

    u64 spec_ghr = m_ghr;
    for (u32 i = 0; i < nslots; i++) {
        if ((cond_mask & (1u << i)) == 0) {
            continue;
        }
        pc_t pc = base + static_cast<pc_t>(i) * 4;
        bool taken = m_table[index(pc, spec_ghr)].predict();
        predict.pred[i].valid = true;
        predict.pred[i].taken = taken;
        predict.pred[i].type = BrType::COND;
        spec_ghr = advance(spec_ghr, taken);
        LOG_LEVEL1("BIM", "Predict pc:0x%lx ghr:0x%lx taken: %u", pc, spec_ghr, taken);
        break;
    }
    m_ghr = spec_ghr;
    return predict;
}

bool BIM::peek(pc_t pc) const
{
    return m_table[index(pc, m_ghr)].predict();
}

void BIM::update(pc_t pc, bool taken, u64 ghr)
{
    m_table[index(pc, ghr)].update(taken);
}

void BIM::shift(bool taken)
{
    m_ghr = advance(m_ghr, taken);
}

void BIM::rollback(u64 ghr)
{
    m_ghr = ghr & hist_mask();
}