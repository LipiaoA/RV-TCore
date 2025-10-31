#ifndef TCORE_RASCACHE_H
#define TCORE_RASCACHE_H


struct RASCacheEntry {
    bool valid=false;
    u64 call_pc_tag=0; // tag derived from call PC
    u64 ret_target=0;
    uint32_t last_used=0;
};

class RASCache {
public:
    RASCache(int entries, int tagbits): m_entries(entries), m_tagbits(tagbits) { m_arr.resize(entries); }
    // simple fully-assoc probe
    pair<bool,u64> probe(u64 call_pc, uint32_t time) {
        u64 tag = call_pc >> 2; tag &= ((1ull<<m_tagbits)-1);
        int found=-1;
        for (int i=0;i<m_entries;++i) if (m_arr[i].valid && m_arr[i].call_pc_tag==tag) { found=i; break; }
        if (found>=0) { m_arr[found].last_used=time; return {true, m_arr[found].ret_target}; }
        return {false,0};
    }
    void insert(u64 call_pc, u64 ret_target, uint32_t time) {
        u64 tag = call_pc >> 2; tag &= ((1ull<<m_tagbits)-1);
        // find invalid
        for (int i=0;i<m_entries;++i) if (!m_arr[i].valid) {
            m_arr[i].valid=true; m_arr[i].call_pc_tag=tag; m_arr[i].ret_target=ret_target; m_arr[i].last_used=time; return;
        }
        // replace LRU
        int lru=0; uint32_t lrutime=m_arr[0].last_used;
        for (int i=1;i<m_entries;++i) if (m_arr[i].last_used < lrutime) { lru=i; lrutime=m_arr[i].last_used; }
        m_arr[lru].call_pc_tag=tag; m_arr[lru].ret_target=ret_target; m_arr[lru].last_used=time;
    }
private:
    int m_entries;
    int m_tagbits;
    vector<RASCacheEntry> m_arr;
};

#endif
