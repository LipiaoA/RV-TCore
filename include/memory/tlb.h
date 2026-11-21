
#ifndef RV_TCORE_TLB_H
#define RV_TCORE_TLB_H

#include "rv_def.h"
#include "util.h"
#include "perf/perf_def.h"

struct TLBResult { bool ok; u64 pa; bool page_fault; std::string reason; };

struct PTWLevelConfig {
    u64 latency = 10; // cycles per level
    bool present = true; // whether level lookup succeeds; in real system this is based on page tables
};

class MultiLevelTLB {
public:
    MultiLevelTLB(CycleScheduler* sched, size_t page_bits=12, std::vector<PTWLevelConfig> levels = {});
    ~MultiLevelTLB() { return; };
    // Synchronous lookup: consult cached mappings
    std::pair<bool, u64>                        lookup(u64 va);
    std::pair<bool, u64>                        lookup_a(u64 va);

    // Asynchronous translate that models PTW across levels and possible faults.
    SimpleFuture<TLBResult>                     translate_async(u64 va);
    const CacheStats&                           stats() const { return m_stats; }

private:
    CycleScheduler*                             m_sched;
    size_t                                      m_page_bits;
    std::vector<PTWLevelConfig>                 m_levels;
    std::unordered_map<u64,u64>                 m_map; // vpn -> pfn
    std::mutex                                  m_mtx;
    bool                                        m_use_vpn { false };
    CacheStats                                  m_stats {};
};

#endif
