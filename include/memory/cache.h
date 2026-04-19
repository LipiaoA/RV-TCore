#ifndef RV_TCORE_CACHE_H
#define RV_TCORE_CACHE_H

#include <bits/stdc++.h>

#include "rv_def.h"
#include "util.h"
#include "mem.h"
#include "pseudo_lru.h"

// ----------------------------- Configs ---------------------------------
struct CacheConfig {
    u32 size_bytes = 32 * 1024;
    u32 line_size = 64;
    u32 ways = 4;
    u32 fetch_width = 4;
    // latencies in cycles
    u32 hit_latency = 1;
    u32 miss_penalty = 10;   // time to start lower request
    u32 install_latency = 1; // time to install after data arrives
    u32 writeback_latency = 5;
    CacheConfig() = default;
    CacheConfig(u32 s, u32 l, u32 w, u32 fw=4):
        size_bytes(s), line_size(l), ways(w), fetch_width(fw) {}
};

enum class InclusionPolicy { NonInclusive, Inclusive, Exclusive };

// --------------------------- Generic Cache ------------------------------
struct LineMeta { bool valid=false; u64 tag=0; bool dirty=false; u64 last_access=0; };

class GCache {
    struct MSHREntry { u64 line_addr; std::vector<SimpleFuture<std::vector<u8>>> promises; };
    typedef struct Stats { u64 accesses=0, hits=0, misses=0, total_latency=0; } Stats;

public:
    GCache(CycleScheduler *sched, const CacheConfig &cfg, InclusionPolicy pol = InclusionPolicy::NonInclusive);
    virtual ~GCache();

    void                                set_name(std::string name) {m_name = move(name);}
    void                                attach_lower(GCache* lower);
    void                                register_upper(GCache* up);

    // Stats
    Stats                               get_stats() const { return m_stats; }

    // async fetch returns future completing with data; records latency into stats when fulfilled
    SimpleFuture<std::vector<u8>>       fetch_line_async(u64 line_addr);

    // write_line_async used by upper caches to write back
    SimpleFuture<bool>                  write_line_async(u64 line_addr, const std::vector<u8>& data);

    // expose invalidate used by inclusive policy
    void                                invalidate_line(u64 line_addr);
    virtual std::vector<u8>             on_lower_fetch(u64 line_addr);
    void                                set_mem(Memory* mem) {m_mem = mem;}

protected:

    // helper decode
    std::pair<size_t, u64>              decode(u64 line_addr) const;
    u64                                 reconstruct_line_addr(size_t set_idx, u64 tag) const;

    // update LRU list on access (move way to MRU front)
    void                                update_lru_list_on_access(size_t set_idx, size_t way);

    void                                install_line(u64 line_addr, const std::vector<u8>& data, bool mark_dirty);

    void                                fulfill_mshr(u64 line_addr, const std::vector<u8>& data, u64 start_cycle);
protected:
    CycleScheduler*                                                 m_sched;
    CacheConfig                                                     m_cfg;
    InclusionPolicy                                                 m_policy;
    size_t                                                          m_sets;

    std::vector<std::vector<std::vector<u8>>>                       m_data; // [set][way][byte]
    std::vector<std::vector<LineMeta>>                              m_meta; // [set][way]
    std::vector<PseudoLRU>                                          m_plru; // per-set
    std::vector<std::vector<size_t>>                                m_lru_lists; // fallback LRU ordering per set

    GCache*                                                         m_lower = nullptr;
    std::vector<GCache*>                                            m_uppers;
    std::mutex                                                      m_up_mtx;

    std::unordered_map<u64, MSHREntry>                              m_mshr;
    mutable std::mutex                                              m_mtx;

    u64                                                             m_tick = 1;
    PseudoLRU                                                       m_global_plru;
    Stats                                                           m_stats;
    mutable std::mutex                                              m_stats_mtx;
    std::string                                                     m_name;
    Memory*                                                         m_mem;
};

#endif
