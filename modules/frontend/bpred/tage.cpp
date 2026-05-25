#include "tage.h"


TAGE::TAGE(const TageConfig &cfg)
    : m_ntables(cfg.ntables)
{
    assert(m_ntables <= MAX_TABLES);
    u32 idx_bits = 10;
    for (u32 i = 0; i < m_ntables; i++) {
        m_tables[i].hist_len = cfg.hist_len[i];
        m_tables[i].idx_bits = idx_bits;
        m_tables[i].tag_bits = cfg.tag_bits[i];
        m_tables[i].size = cfg.table_size[i];
        m_tables[i].tag_mask = (1 << cfg.tag_bits[i]) - 1;
        m_tables[i].fold_idx = FoldedHistory(cfg.hist_len[i], idx_bits);
        m_tables[i].fold_tag1 = FoldedHistory(cfg.hist_len[i], cfg.tag_bits[i]);
        m_tables[i].fold_tag2 = FoldedHistory(cfg.hist_len[i], cfg.tag_bits[i] - 1);
    }
    m_ghr = 0ull;
}

TagePred TAGE::predict(u64 pc)
{
    TagePred pred;
    for (i32 t = m_ntables - 1; t >= 0; t--) {
        u32 idx = index(t, pc);
        u16 tag  = ctag(t, pc);

        auto &e = m_tables[t].entries[idx];

        if (e.tag == tag) {
            if (pred.provider == -1) {
                pred.provider = t;
            } else if (pred.alt_provider == -1) {
                pred.alt_provider = t;
            }
        }
    }

    if (pred.provider == -1) {
        return pred;
    }

    bool provider_pred = m_tables[pred.provider].entries[index(pred.provider, pc)].predict();
    pred.valid = true;
    pred.final_pred = provider_pred;

    if (pred.alt_provider != -1) {
        pred.alt_pred  = m_tables[pred.alt_provider].entries[index(pred.alt_provider, pc)].predict();
    }

    auto &entry = m_tables[pred.provider].entries[index(pred.provider, pc)];
    if (entry.u == 0 && std::abs(entry.ctr) == 0)  {
        /* 如果provider的表项是新进弱taken，并且没有alt_provider，则预测失败 */
        if (pred.alt_provider != -1) {
            pred.valid = true;
            pred.final_pred = pred.alt_pred;
        } else {
            pred.valid = false;
        }
    }

    return pred;
}

void TAGE::update(u64 pc, bool taken, TagePred &pred)
{
    if (pred.provider == -1) {
        return;
    }

    auto &e = m_tables[pred.provider].entries[index(pred.provider, pc)];
    e.update(taken);

    /* usefulness update  */
    bool provider_correct = (pred.final_pred == taken);
    bool alt_correct = (pred.alt_pred == taken);
    if (provider_correct != alt_correct) {
        if (provider_correct && e.u < 3) {
            e.u++;
        } else if (!provider_correct && e.u > 0) {
            e.u--;
        }
    }

    bool found = false;
    /* 如果provider的预测失败 */
    if(pred.final_pred != taken) {
        for(int t = pred.provider + 1; t < m_ntables; t++) {
            auto &e = m_tables[t].entries[index(t, pc)];
            if(e.u == 0) {
                e.tag = ctag(t, pc);
                e.ctr = taken ? 0 : -1;
                e.u   = 0;
                found = true;
                break;
            }
        }
    }

    // we should update u in not found situation
    assert(found);

    // 更新全局历史
    m_ghr = ((m_ghr << 1) | taken) & ((1 << 128) - 1);

    for(int i = 0; i < m_ntables; i++)
    {
        auto &fold_idx = m_tables[i].fold_idx;
        auto &fold_tag1 = m_tables[i].fold_tag1;
        auto &fold_tag2 = m_tables[i].fold_tag2;
        fold_idx.update(taken);
        fold_tag1.update(taken);
        fold_tag2.update(taken);
    }
    return;
}

u32 TAGE::index(i32 t, pc_t pc) const
{
    return pc ^ m_tables[t].fold_idx.value();
}

u16 TAGE::ctag(i32 t, pc_t pc) const
{
    auto tag1_val = m_tables[t].fold_tag1.value();
    auto tag2_val = m_tables[t].fold_tag2.value();
    auto tag_mask = m_tables[t].tag_mask;

    return (pc ^ tag1_val ^ (tag2_val << 1)) & tag_mask;
}
