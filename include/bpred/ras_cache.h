#ifndef TCORE_RASCACHE_H
#define TCORE_RASCACHE_H

#include "util.h"

struct RASCacheEntry {
    bool valid=false;
    u64 call_pc_tag=0; // tag derived from call PC
    u64 ret_target=0;
    u32 last_used=0;
};

class RASCache {
public:
    RASCache(int entries, int tagbits);
    // simple fully-assoc probe
    std::pair<bool,u64>             probe(u64 call_pc, u32 time);
    void                            insert(u64 call_pc, u64 ret_target, u32 time);
private:
    int                             m_entries;
    int                             m_tagbits;
    std::vector<RASCacheEntry>      m_arr;
};

#endif
