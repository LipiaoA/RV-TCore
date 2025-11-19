
#include "ras_cache.h"

RASCache::RASCache(int entries, int tagbits) : m_entries(entries), m_tagbits(tagbits)
{
    m_arr.resize(entries);
}

std::pair<bool, u64> RASCache::probe(u64 call_pc, u32 time)
{
    u64 tag = call_pc >> 2;
    tag &= ((1ull << m_tagbits) - 1);
    int found = -1;
    for (int i = 0; i < m_entries; ++i) {
        if (m_arr[i].valid && m_arr[i].call_pc_tag == tag) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        m_arr[found].last_used = time;
        return {true, m_arr[found].ret_target};
    }
    return {false, 0};
}

void RASCache::insert(u64 call_pc, u64 ret_target, u32 time)
{
    u64 tag = call_pc >> 2;
    tag &= ((1ull << m_tagbits) - 1);
    // find invalid
    for (int i = 0; i < m_entries; ++i) {
        if (!m_arr[i].valid) {
            m_arr[i].valid = true;
            m_arr[i].call_pc_tag = tag;
            m_arr[i].ret_target = ret_target;
            m_arr[i].last_used = time;
            return;
        }
    }
    // replace LRU
    int lru = 0;
    u32 lrutime = m_arr[0].last_used;
    for (int i = 1; i < m_entries; ++i) {
        if (m_arr[i].last_used < lrutime) {
            lru = i;
            lrutime = m_arr[i].last_used;
        }
    }
    m_arr[lru].call_pc_tag = tag;
    m_arr[lru].ret_target = ret_target;
    m_arr[lru].last_used = time;
}
