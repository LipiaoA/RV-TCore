#include "store_predictor.h"

StoreSetPredictor::StoreSetPredictor()
{
    memset(m_ssit, 0, sizeof(m_ssit));
    for (auto& x : m_lfst) x = -1;
}

StoreSetPredictor::~StoreSetPredictor()
{
}


u32 StoreSetPredictor::get_or_create_ssid(u64 pc)
{
    u32& ssid = m_ssit[pc % SSIT_SIZE];
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
    u32 s1 = m_ssit[load_pc % SSIT_SIZE];
    u32 s2 = m_ssit[store_pc % SSIT_SIZE];
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
        if (old_id < LFST_SIZE && new_id < LFST_SIZE && m_lfst[new_id] < 0) {
            m_lfst[new_id] = m_lfst[old_id];
        }
    }
    m_ssit[load_pc % SSIT_SIZE] = new_id;
    m_ssit[store_pc % SSIT_SIZE] = new_id;
    return new_id;
}

void StoreSetPredictor::update_lfst(u32 ssid, u64 store_id)
{
    if (ssid > 0 && ssid < LFST_SIZE) {
        m_lfst[ssid] = static_cast<i64>(store_id);
    }
}

i64 StoreSetPredictor::get_lfst(u32 ssid) const
{
    if (ssid == 0 || ssid >= LFST_SIZE) {
        return -1;
    }
    return m_lfst[ssid];
}

void StoreSetPredictor::clear_lfst(u32 ssid, u64 store_id)
{
    if (ssid > 0 && ssid < LFST_SIZE && m_lfst[ssid] == static_cast<i64>(store_id)) {
        m_lfst[ssid] = -1;
    }
}
