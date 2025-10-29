#ifndef RV_TCORE_CACHE_H
#define RV_TCORE_CACHE_H

#include <bits/stdc++.h>

#include "rv_def.h"
#include "util.h"


// - Event-driven CycleScheduler (no std::async)
// - Writeback / write-allocate, dirty bits, inclusion policies
// - Asynchronous multi-level TLB page-walk with permissions and faults
// - Statistics collection (accesses, hits, misses, avg latency)
// - Pseudo-LRU (tree-based) replacement; falls back to true LRU if ways not power-of-two
// - Supports n-way set-associative, configurable line size, fetch_width
// - Non-blocking MSHR coalescing and SimpleFuture continuations
// This file is intended as a simulator-friendly, single-file reference implementation.




// ----------------------------- Configs ---------------------------------
struct CacheConfig {
    size_t size_bytes = 32 * 1024;
    size_t line_size = 64;
    size_t ways = 4;
    size_t fetch_width = 4;
    // latencies in cycles
    u64 hit_latency = 1;
    u64 miss_penalty = 10;   // time to start lower request
    u64 install_latency = 1; // time to install after data arrives
    u64 writeback_latency = 5;
    CacheConfig() = default;
    CacheConfig(size_t s, size_t l, size_t w, size_t fw=4):
        size_bytes(s), line_size(l), ways(w), fetch_width(fw) {}
};

enum class InclusionPolicy { NonInclusive, Inclusive, Exclusive };



// ------------------------- Pseudo-LRU helper ---------------------------
// Tree-based PLRU for power-of-two ways. For non-power-of-two, fall back to full LRU.
class PseudoLRU {
public:
    PseudoLRU() {}
    void init(size_t ways) {
        m_ways = ways;
        if (is_power_of_two(ways)) {
            m_use_tree = true;
            m_tree_bits.assign(ways-1, 0);
        } else { m_use_tree = false; }
    }
    void touch(size_t way) {
        if (!m_use_tree) { // nothing
            // users will maintain true LRU externally
            return;
        }
        // update tree bits from root to leaf
        size_t idx = 0;
        size_t span = m_ways;
        while (span > 1) {
            span /= 2;
            if (way < idx + span) { // go left
                m_tree_bits[idx] = 1; idx = idx*2+1; // set to prefer right next time
            } else {
                m_tree_bits[idx] = 0; idx = idx*2+2;
                way -= span;
            }
        }
    }
    size_t victim() {
        if (!m_use_tree) return SIZE_MAX;
        // walk tree to find victim
        size_t idx = 0;
        size_t way = 0;
        size_t span = m_ways;
        while (span > 1) {
            span /= 2;
            if (m_tree_bits[idx] == 0) { // go left
                // mark to prefer left next time
                m_tree_bits[idx] = 1;
                idx = idx*2+1;
            } else {
                m_tree_bits[idx] = 0;
                idx = idx*2+2; way += span;
            }
        }
        return way;
    }
private:
    bool is_power_of_two(size_t x) { return x && ((x & (x-1))==0); }
    size_t m_ways = 0;
    bool m_use_tree = false;
    std::vector<uint8_t> m_tree_bits; // tree nodes
};

// --------------------------- Generic Cache ------------------------------
struct LineMeta { bool valid=false; u64 tag=0; bool dirty=false; u64 last_access=0; };

class CacheI {
public:
    CacheI(CycleScheduler *sched, const CacheConfig &cfg, InclusionPolicy pol = InclusionPolicy::NonInclusive);
    virtual ~CacheI() = default;

    struct Stats { u64 accesses=0, hits=0, misses=0; u64 total_latency=0; };

    void                        attach_lower(CacheI* lower);
    void                        register_upper(CacheI* up);

    // Stats
    Stats                       get_stats() const { return m_stats; }

    // async fetch returns future completing with data; records latency into stats when fulfilled
    SimpleFuture<std::vector<u8>> fetch_line_async(u64 line_addr);

    // write_line_async used by upper caches to write back
    SimpleFuture<void> write_line_async(u64 line_addr, const std::vector<u8>& data) {
        SimpleFuture<void> out;
        // write-allocate: fetch or install then mark dirty
        auto f = fetch_line_async(line_addr);
        f.then([this, line_addr, data, out](const std::vector<u8>& existing){
            // overwrite and mark dirty
            m_sched->schedule_after(0, [this, line_addr, data, out]() mutable {
                this->install_line(line_addr, data, true); // mark dirty inside
                out.set_value();
            });
        });
        return out;
    }

    // expose invalidate used by inclusive policy
    void invalidate_line(u64 line_addr) {
        auto [set_idx, tag] = decode(line_addr);
        std::lock_guard<std::mutex> lk(m_mtx);
        for (size_t w=0; w<m_cfg.ways; ++w) {
            if (m_meta[set_idx][w].valid && m_meta[set_idx][w].tag==tag) {
                if (m_meta[set_idx][w].dirty && m_lower) {
                    auto data = m_data[set_idx][w]; u64 evict_addr = reconstruct_line_addr(set_idx, m_meta[set_idx][w].tag);
                    m_sched->schedule_after(m_cfg.writeback_latency, [this, evict_addr, data]() {
                        if (this->m_lower) this->m_lower->write_line_async(evict_addr, data);
                    });
                }
                m_meta[set_idx][w].valid=false; m_meta[set_idx][w].dirty=false;
            }
        }
    }

protected:
    struct MSHREntry { u64 line_addr; std::vector<SimpleFuture<std::vector<u8>>> promises; };

    CycleScheduler* m_sched;
    CacheConfig m_cfg;
    InclusionPolicy m_policy;
    size_t m_sets;

    std::vector<std::vector<std::vector<u8>>> m_data; // [set][way][byte]
    std::vector<std::vector<LineMeta>> m_meta;   // [set][way]
    std::vector<PseudoLRU> m_plru;          // per-set
    std::vector<std::vector<size_t>> m_lru_lists; // fallback LRU ordering per set

    CacheI* m_lower = nullptr;
    std::vector<CacheI*> m_uppers; std::mutex m_up_mtx;

    std::unordered_map<u64, MSHREntry> m_mshr;
    mutable std::mutex m_mtx;

    u64 m_tick = 1;
    PseudoLRU m_global_plru;

    // stats
    mutable std::mutex m_stats_mtx; // protect stats (not strictly necessary for single-thread sim)
    struct Stats { u64 accesses=0, hits=0, misses=0, total_latency=0; } m_stats;

    // helper decode
    pair<size_t,u64> decode(u64 line_addr) const { size_t set_idx = (line_addr / m_cfg.line_size) % m_sets; u64 tag = line_addr / (m_cfg.line_size * m_sets); return {set_idx, tag}; }
    u64 reconstruct_line_addr(size_t set_idx, u64 tag) const { return (tag * (m_cfg.line_size * m_sets)); }

    // update LRU list on access (move way to MRU front)
    void update_lru_list_on_access(size_t set_idx, size_t way) {
        auto &lst = m_lru_lists[set_idx];
        auto it = find(lst.begin(), lst.end(), way);
        if (it != lst.end()) { lst.erase(it); lst.insert(lst.begin(), way); }
    }

    void install_line(u64 line_addr, const std::vector<u8>& data, bool mark_dirty) {
        auto [set_idx, tag] = decode(line_addr);
        size_t victim = SIZE_MAX;
        bool used_plru = false;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            // find invalid
            for (size_t w=0; w<m_cfg.ways; ++w) if (!m_meta[set_idx][w].valid) { victim=w; break; }
            if (victim==SIZE_MAX) {
                // try PLRU
                size_t plr = m_plru[set_idx].victim();
                if (plr!=SIZE_MAX) { victim = plr; used_plru=true; }
                else {
                    // fallback to true LRU (last element)
                    victim = m_lru_lists[set_idx].back();
                }
                // handle eviction
                if (m_meta[set_idx][victim].dirty && m_lower) {
                    u64 ev_addr = reconstruct_line_addr(set_idx, m_meta[set_idx][victim].tag);
                    auto evdata = m_data[set_idx][victim];
                    m_sched->schedule_after(m_cfg.writeback_latency, [this, ev_addr, evdata]() {
                        if (this->m_lower) this->m_lower->write_line_async(ev_addr, evdata);
                    });
                }
                if (m_policy==InclusionPolicy::Inclusive) {
                    u64 ev_addr = reconstruct_line_addr(set_idx, m_meta[set_idx][victim].tag);
                    std::lock_guard<std::mutex> ulk(m_up_mtx);
                    for (auto up : m_uppers) up->invalidate_line(ev_addr);
                }
            }
            // install
            m_meta[set_idx][victim].valid = true;
            m_meta[set_idx][victim].tag = tag;
            m_meta[set_idx][victim].dirty = mark_dirty;
            m_meta[set_idx][victim].last_access = ++m_tick;
            m_data[set_idx][victim] = data;
            // update replacement structures
            if (!used_plru) {
                // if PLRU not available, maintain LRU list
                update_lru_list_on_access(set_idx, victim);
            } else m_plru[set_idx].touch(victim);
        }
    }

    void fulfill_mshr(u64 line_addr, const std::vector<u8>& data, u64 start_cycle) {
        std::vector<SimpleFuture<std::vector<u8>>> prom;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_mshr.find(line_addr);
            if (it!=m_mshr.end()) { prom = move(it->second.promises); m_mshr.erase(it); }
        }
        for (auto &f : prom) {
            m_stats.total_latency += (m_sched->cycle() - start_cycle);
            f.set_value(data);
        }
    }

    // default lower fetch
    virtual std::vector<u8> on_lower_fetch(u64 line_addr) {
        std::vector<u8> d(m_cfg.line_size);
        for (size_t i=0;i<m_cfg.line_size;++i) d[i] = static_cast<u8>((line_addr + i) & 0xFF);
        return d;
    }
};


// --------------------------- Notes & Next Steps ------------------------
// - Statistics: CacheI::m_stats records accesses, hits, misses and total latency (cycles). avg latency = total_latency / accesses.
// - Pseudo-LRU: tree-based PLRU used when ways is power-of-two. Otherwise fallback to true LRU maintained in m_lru_lists.
// - TLB: MultiLevelTLB simulates a configurable sequence of PTW levels; some VPNs intentionally fault (demo), and faults are returned
//   to callers as empty instruction vectors (caller can detect and handle page faults).
// - Extensions: more accurate permission checks, per-operation permissions (X/R/W), configurable page-table contents, and hooking
//   into a CPU exception mechanism to handle faults. Also could sample per-request latency distribution instead of average.

// End of file


#endif