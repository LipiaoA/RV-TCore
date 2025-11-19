
#include "tlb.h"

using namespace std;

MultiLevelTLB::MultiLevelTLB(CycleScheduler* sched, size_t page_bits, vector<PTWLevelConfig> levels)
    : m_sched(sched), m_page_bits(page_bits), m_levels(levels)
{
    if (m_levels.empty()) {
        m_levels.push_back(PTWLevelConfig());
    }
    m_use_vpn = true;
}

pair<bool, u64> MultiLevelTLB::lookup(u64 va) {
    u64 vpn = va >> m_page_bits;
    u64 offset = va & ((1ULL << m_page_bits) - 1);

    lock_guard<mutex> lk(m_mtx);
    auto it = m_map.find(vpn);
    if (it == m_map.end() || m_use_vpn) {
        // FIXME: adjust to real translate process.
        return {true, (vpn << m_page_bits) | offset };
    }
    return {true, (it->second << m_page_bits) | offset};
}

SimpleFuture<TLBResult> MultiLevelTLB::translate_async(u64 va) {
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
        // otherwise map page: identity mapping for demo
        u64 pfn = vpn;
        {
            lock_guard<mutex> lk(m_mtx); if (m_map.size() > 4096) m_map.erase(m_map.begin()); m_map[vpn] = pfn;
        }
        u64 pa = (pfn << m_page_bits) | (va & ((1ULL<<m_page_bits)-1));
        fut.set_value({true, pa, false, ""});
    });
    return fut;
}
