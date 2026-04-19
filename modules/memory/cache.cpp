#include <stdexcept>
#include "cache.h"
#include "log.h"

GCache::GCache(CycleScheduler *sched, const CacheConfig &cfg)
    : m_sched(sched),
      m_cfg(cfg),
      m_policy(cfg.inclusion_policy),
      m_mem(nullptr)
{
    if ((m_cfg.line_size & (m_cfg.line_size - 1)) != 0)
        throw std::runtime_error("line_size must be power of two");
    m_sets = (m_cfg.size) / (m_cfg.line_size * m_cfg.ways);
    if (m_sets == 0)
        throw std::runtime_error("invalid cache configuration");
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
    m_name = "unknown";
}

void GCache::attach_lower(GCache *lower) {
    m_lower = lower;
    if (m_lower) {
        m_lower->register_upper(this);
    }
    return;
}

void GCache::register_upper(GCache *up) {
    std::lock_guard<std::mutex> lk(m_up_mtx);
    m_uppers.push_back(up);
}

SimpleFuture<std::vector<u8>> GCache::fetch_line_async(u64 line_addr) {
    m_stats.accesses++;
    SimpleFuture<std::vector<u8>> fut;
    auto start_cycle = m_sched->cycle();
    auto [set_idx, tag] = decode(line_addr);

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        // search for hit
        for (size_t w = 0; w < m_cfg.ways; ++w) {
            if (m_meta[set_idx][w].valid && m_meta[set_idx][w].tag == tag) {
                LOG_LEVEL1(m_name, " Hit addr:0x%x, set_idx:%d, way:%d, tag:0x%x\n", line_addr, set_idx, w, tag);
                m_stats.hits++;
                m_meta[set_idx][w].last_access = ++m_tick;
                // update replacement structures
                m_plru[set_idx].touch(w);
                update_lru_list_on_access(set_idx, w);
                // schedule ready after hit latency
                auto data = m_data[set_idx][w];
                m_sched->schedule_after(m_cfg.hit_latency, [this, fut, data, start_cycle]() mutable {
                    m_stats.total_latency += (m_sched->cycle() - start_cycle);
                    fut.set_value(data); });
                return fut;
            }
        }
        LOG_LEVEL1(m_name, " Miss addr:0x%x, set_idx:%d, tag:0x%x\n", line_addr, set_idx, tag);

        // miss
        m_stats.misses++;
        auto it = m_mshr.find(line_addr);
        if (it != m_mshr.end()) {
            it->second.promises.push_back(fut);
            return fut;
        }
        MSHREntry me;
        me.line_addr = line_addr;
        me.promises.push_back(fut);
        m_mshr.emplace(line_addr, std::move(me));
    }
    // schedule lower request after miss_penalty
    m_sched->schedule_after(m_cfg.miss_penalty, [this, line_addr, start_cycle]() {
        if (m_lower) {
            auto lf = m_lower->fetch_line_async(line_addr);

            LOG_LEVEL1(m_name, "send req addr:%x\n", line_addr);

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


void GCache::update_lru_list_on_access(size_t set_idx, size_t way) {
    auto &lst = m_lru_lists[set_idx];
    auto it = find(lst.begin(), lst.end(), way);
    if (it != lst.end()) { lst.erase(it); lst.insert(lst.begin(), way); }
}

void GCache::install_line(u64 line_addr, const std::vector<u8>& data, bool mark_dirty) {
    auto [set_idx, tag] = decode(line_addr);
    size_t victim = SIZE_MAX;
    bool used_plru = false;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        // find invalid
        for (size_t w = 0; w < m_cfg.ways; ++w) {
            if (!m_meta[set_idx][w].valid) {
                victim = w;
                break;
            }
        }
        if (victim==SIZE_MAX) {
            // try PLRU
            size_t plr = m_plru[set_idx].victim();
            if (plr != SIZE_MAX) {
                victim = plr;
                used_plru=true;
            } else {
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
        } else {
            m_plru[set_idx].touch(victim);
        }
    }
}

void GCache::fulfill_mshr(u64 line_addr, const std::vector<u8>& data, u64 start_cycle) {
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
std::vector<u8> GCache::on_lower_fetch(u64 line_addr) {
    if (m_mem) {
        return m_mem->load_cache_line(line_addr, m_cfg.line_size);
    }

    std::cout << "error memory not be set" << std::endl;
    std::vector<u8> d(m_cfg.line_size);
    for (size_t i = 0; i < m_cfg.line_size; ++i) {
        d[i] = static_cast<u8>((line_addr + i) & 0xFF);
    }
    return d;
}


SimpleFuture<bool> GCache::write_line_async(u64 line_addr, const std::vector<u8>& data) {
    SimpleFuture<bool> out;
    // write-allocate: fetch or install then mark dirty
    auto f = fetch_line_async(line_addr);
    f.then([this, line_addr, data, out](const std::vector<u8>& existing){
        // overwrite and mark dirty
        m_sched->schedule_after(0, [this, line_addr, data, out]() mutable {
            this->install_line(line_addr, data, true); // mark dirty inside
            out.set_value(true);
        });
    });
    return out;
}

// expose invalidate used by inclusive policy
void GCache::invalidate_line(u64 line_addr) {
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

GCache::~GCache() {}

std::pair<size_t, u64> GCache::decode(u64 line_addr) const
{
    size_t set_idx = (line_addr / m_cfg.line_size) % m_sets;
    u64 tag = line_addr / (m_cfg.line_size * m_sets);
    return {set_idx, tag};
}

u64 GCache::reconstruct_line_addr(size_t set_idx, u64 tag) const
{
    return (tag * (m_cfg.line_size * m_sets));
}
