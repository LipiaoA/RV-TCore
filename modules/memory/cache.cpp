#include "cache.h"

CacheI::CacheI(CycleScheduler *sched, const CacheConfig &cfg, InclusionPolicy pol)
    : m_sched(sched), m_cfg(cfg), m_policy(pol) {
    if ((m_cfg.line_size & (m_cfg.line_size - 1)) != 0)
        throw runtime_error("line_size must be power of two");
    m_sets = (m_cfg.size_bytes) / (m_cfg.line_size * m_cfg.ways);
    if (m_sets == 0)
        throw runtime_error("invalid cache configuration");
    m_data.resize(m_sets);
    m_meta.resize(m_sets);
    m_plru.resize(m_sets);
    m_lru_lists.resize(m_sets);
    for (size_t s = 0; s < m_sets; ++s) {
        m_data[s].resize(m_cfg.ways, std::vector<u8>(m_cfg.line_size));
        m_meta[s].resize(m_cfg.ways);
        m_plru[s].init(m_cfg.ways);
        // prepare LRU list for fallback
        m_lru_lists[s].resize(m_cfg.ways);
        for (size_t w = 0; w < m_cfg.ways; ++w)
            m_lru_lists[s][w] = w;
    }
}

void CacheI::attach_lower(CacheI *lower) {
    m_lower = lower;
    if (m_lower) {
        m_lower->register_upper(this);
    }
    return;
}

void CacheI::register_upper(CacheI *up) {
    std::lock_guard<std::mutex> lk(m_up_mtx);
    m_uppers.push_back(up);
}

SimpleFuture<std::vector<u8>> CacheI::fetch_line_async(u64 line_addr) {
    m_stats.accesses++;
    SimpleFuture<std::vector<u8>> fut;
    auto start_cycle = m_sched->cycle();
    auto [set_idx, tag] = decode(line_addr);
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        // search for hit
        for (size_t w=0; w<m_cfg.ways; ++w) {
            if (m_meta[set_idx][w].valid && m_meta[set_idx][w].tag==tag) {
                m_stats.hits++;
                m_meta[set_idx][w].last_access = ++m_tick;
                // update replacement structures
                m_plru[set_idx].touch(w);
                update_lru_list_on_access(set_idx, w);
                // schedule ready after hit latency
                auto data = m_data[set_idx][w];
                m_sched->schedule_after(m_cfg.hit_latency, [this, fut, data, start_cycle]() mutable {
                    m_stats.total_latency += (m_sched->cycle() - start_cycle);
                    fut.set_value(data);
                });
                return fut;
            }
        }
        // miss
        m_stats.misses++;
        auto it = m_mshr.find(line_addr);
        if (it != m_mshr.end()) { it->second.promises.push_back(fut); return fut; }
        MSHREntry me; me.line_addr = line_addr; me.promises.push_back(fut); m_mshr.emplace(line_addr, move(me));
    }
    // schedule lower request after miss_penalty
    m_sched->schedule_after(m_cfg.miss_penalty, [this, line_addr, start_cycle]() {
        if (m_lower) {
            auto lf = m_lower->fetch_line_async(line_addr);
            lf.then([this, line_addr, start_cycle](const std::vector<u8>& data) {
                // install after install_latency
                m_sched->schedule_after(m_cfg.install_latency, [this, line_addr, data, start_cycle]() {
                    this->install_line(line_addr, data, false);
                    this->fulfill_mshr(line_addr, data, start_cycle);
                });
            });
        } else {
            // main memory
            auto data = this->on_lower_fetch(line_addr);
            m_sched->schedule_after(m_cfg.install_latency, [this, line_addr, data, start_cycle]() {
                this->install_line(line_addr, data, false);
                this->fulfill_mshr(line_addr, data, start_cycle);
            });
        }
    });
    return fut;
}


