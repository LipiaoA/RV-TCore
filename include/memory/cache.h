#ifndef RV_TCORE_CACHE_H
#define RV_TCORE_CACHE_H

#include <bits/stdc++.h>

#include "rv_def.h"
#include "util.h"
#include "mem.h"
#include "pseudo_lru.h"
#include "config.h"
#include "module_comm.h"

// --------------------------- Generic Cache ------------------------------
struct LineMeta { bool valid=false; u64 tag=0; bool dirty=false; u64 last_access=0; };

typedef struct MemReq {
    bool valid { false };
    bool write { false };
    pc_t line_addr { 0 };
    u8 data[64];
    u64 epoch { 0 };
} MemReq;

typedef struct MemResp {
    bool valid { false };
    pc_t line_addr { 0 };
    u8 data[64];
    bool page_fault { false };
    bool access_fault { false };
    u64 epoch { 0 };
} MemResp;

class GCache {
    struct PendingStore {
        StoreReq req;
        SimpleFuture<bool> fut;
        u64 start_cycle { 0 };
    };

    struct MSHREntry {
        u64 line_addr { 0 };
        u64 epoch { 0 };
        u64 seq { 0 };
        bool lower_in_flight { false };
        std::vector<SimpleFuture<MemResp>> promises;
        std::vector<PendingStore> pending_stores;
    };
    typedef struct Stats { u64 accesses=0, hits=0, misses=0, total_latency=0; } Stats;

public:
    GCache(CycleScheduler *sched, const CacheConfig &cfg);
    virtual ~GCache();

    void                                set_name(std::string name);
    void                                attach_lower(GCache* lower);
    void                                register_upper(GCache* up);

    // Stats
    Stats                               get_stats() const { return m_stats; }

    // async fetch returns future completing with data; records latency into stats when fulfilled
    SimpleFuture<MemResp>               fetch_line_async(MemReq req);

    // write_line_async used by upper caches to write back
    SimpleFuture<bool>                  write_line_async(MemResp resp);

    // write-back store: merge bytes on hit, write-allocate on miss
    SimpleFuture<bool>                  store_merge_async(StoreReq req);

    // expose invalidate used by inclusive policy
    void                                invalidate_line(u64 line_addr);
    virtual std::vector<u8>             on_lower_fetch(u64 line_addr);
    void                                set_mem(Memory* mem);

protected:

    // helper decode
    std::pair<size_t, u64>              decode(u64 line_addr) const;
    u64                                 reconstruct_line_addr(size_t set_idx, u64 tag) const;

    // update LRU list on access (move way to MRU front)
    void                                update_lru_list_on_access(size_t set_idx, size_t way);

    void                                install_line(MemResp resp, bool mark_dirty);

    void                                fulfill_mshr(MemResp resp, u64 start_cycle);

    void                                drop_mshr(u64 line_addr);

    bool                                mshr_fulfillable(u64 line_addr, u64 seq) const;

    bool                                try_read_hit(u64 line_addr, u64 epoch, MemResp& out) const;

    void                                complete_miss(u64 line_addr, MemResp resp, u64 start_cycle, u64 miss_seq);

    void                                send_lower_for_miss(MemReq req, u64 start_cycle, u64 miss_seq);

    static void                         merge_store_data(std::vector<u8>& line, u64 byte_addr,
                                                         u64 line_addr, u64 data, u32 width);

    void                                apply_pending_store_merges(u64 line_addr);

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
    u64                                                             m_mshr_seq { 0 };
    mutable std::mutex                                              m_mtx;

    u64                                                             m_tick = 1;
    PseudoLRU                                                       m_global_plru;
    Stats                                                           m_stats;
    mutable std::mutex                                              m_stats_mtx;
    std::string                                                     m_name;
    Memory*                                                         m_mem;
};

#endif
