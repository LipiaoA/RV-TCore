#include <algorithm>

#include "store_predictor.h"

StoreSetPredictor::StoreSetPredictor()
{
    memset(m_ssit, 0, sizeof(m_ssit));
    for (auto& x : m_lfst) {
        x = -1;
    }
}

StoreSetPredictor::~StoreSetPredictor()
{
}

inline u32 StoreSetPredictor::ssid_idx(u64 pc) const
{
    return (pc >> 2) % SSIT_SIZE;
}

u32 StoreSetPredictor::get_or_create_ssid(u64 pc)
{
    u32 idx = ssid_idx(pc);
    u32& ssid = m_ssit[idx];
    if (ssid == 0) {
        ssid = m_next_id++;
        if (m_next_id >= LFST_SIZE) {
            m_next_id = 1;
        }
    }
    return ssid;
}

u32 StoreSetPredictor::merge_ssid(u64 load_pc, u64 store_pc)
{
    u32 l_idx = ssid_idx(load_pc);
    u32 s_idx = ssid_idx(store_pc);
    u32 s1 = m_ssit[l_idx];
    u32 s2 = m_ssit[s_idx];
    u32 new_id = 0;
    if (s1 == 0 && s2 == 0) {
        new_id = m_next_id++;
        if (m_next_id >= LFST_SIZE) {
            m_next_id = 1;
        }
    } else if (s1 == 0) {
        new_id = s2;
    } else if (s2 == 0) {
        new_id = s1;
    } else {
        new_id = std::min(s1, s2);
        const u32 old_id = std::max(s1, s2);
        // 合并 set 时，若新 id 尚无 in-flight store，继承旧 set 的 LFST。
        if (old_id < LFST_SIZE && new_id < LFST_SIZE && m_lfst[new_id] < 0) {
            m_lfst[new_id] = m_lfst[old_id];
        }
    }
    m_ssit[l_idx] = new_id;
    m_ssit[s_idx] = new_id;
    return new_id;
}

void StoreSetPredictor::update_lfst(u32 ssid, u64 store_id)
{
    if (ssid > 0 && ssid < LFST_SIZE) {
        m_lfst[ssid] = store_id;
    }
}

u64 StoreSetPredictor::get_lfst(u32 ssid) const
{
    if (ssid == 0 || ssid >= LFST_SIZE) {
        return 0;
    }
    return m_lfst[ssid];
}

void StoreSetPredictor::clear_lfst(u32 ssid, u64 store_id)
{
    if (ssid > 0 && ssid < LFST_SIZE
        && m_lfst[ssid] == store_id) {
        m_lfst[ssid] = 0;
    }
}
