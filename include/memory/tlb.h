
#ifndef RV_TCORE_TLB_H
#define RV_TCORE_TLB_H

#include "rv_def.h"
#include "util.h"


// ----------------------------- TLB -------------------------------------
// Enhanced multi-level PTW simulation with permissions and faults.
struct TLBResult { bool ok; u64 pa; bool page_fault; std::string reason; };

struct PTWLevelConfig {
    u64 latency = 10; // cycles per level
    bool present = true; // whether level lookup succeeds; in real system this is based on page tables
};

class MultiLevelTLB {
public:
    MultiLevelTLB(CycleScheduler* sched, size_t page_bits=12, std::vector<PTWLevelConfig> levels = {}) :
        m_sched(sched), m_page_bits(page_bits), m_levels(levels) {
        if (m_levels.empty()) { m_levels.push_back(PTWLevelConfig()); }
    }

    // Synchronous lookup: consult cached mappings
    pair<bool, u64> lookup(u64 va) {
        u64 vpn = va >> m_page_bits; std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_map.find(vpn);
        if (it == m_map.end()) return {false,0};
        u64 offset = va & ((1ULL<<m_page_bits)-1);
        return {true, (it->second<<m_page_bits) | offset};
    }

    // Asynchronous translate that models PTW across levels and possible faults.
    SimpleFuture<TLBResult> translate_async(u64 va) {
        SimpleFuture<TLBResult> fut;
        auto pr = lookup(va);
        if (pr.first) { fut.set_value({true, pr.second, false, ""}); return fut; }
        // simulate PTW across levels serially
        u64 total_lat = 0;
        for (auto &lvl : m_levels) total_lat += lvl.latency;
        m_sched->schedule_after(total_lat, [this, va, fut]() mutable {
            // For demo, decide success/fault based on vpn parity to simulate some faults
            u64 vpn = va >> m_page_bits;
            if ((vpn & 0x3) == 3) {
                // simulate page fault
                fut.set_value({false, 0, true, "simulated page fault"});
                return;
            }
            // otherwise std::map page: identity mapping for demo
            u64 pfn = vpn;
            {
                std::lock_guard<std::mutex> lk(m_mtx); if (m_map.size() > 4096) m_map.erase(m_map.begin()); m_map[vpn] = pfn;
            }
            u64 pa = (pfn << m_page_bits) | (va & ((1ULL<<m_page_bits)-1));
            fut.set_value({true, pa, false, ""});
        });
        return fut;
    }

private:
    CycleScheduler* m_sched;
    size_t m_page_bits;
    std::vector<PTWLevelConfig> m_levels;
    std::unordered_map<u64,u64> m_map; // vpn -> pfn
    std::mutex m_mtx;
};

#endif
