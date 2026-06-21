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

void GCache::attach_lower(GCache *lower)
{
    m_lower = lower;
    if (m_lower) {
        m_lower->register_upper(this);
    }
    return;
}

void GCache::register_upper(GCache *up)
{
    std::lock_guard<std::mutex> lk(m_up_mtx);
    m_uppers.push_back(up);
}

SimpleFuture<MemResp> GCache::fetch_line_async(MemReq req)
{
    m_stats.accesses++;
    SimpleFuture<MemResp> fut;
    auto start_cycle = m_sched->cycle();
    auto [set_idx, tag] = decode(req.line_addr);
    u64 miss_seq = 0;
    bool schedule_lower = false;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (size_t w = 0; w < m_cfg.ways; ++w) {
            if (m_meta[set_idx][w].valid && m_meta[set_idx][w].tag == tag) {
                LOG_LEVEL1(m_name, " Hit addr:0x%x, set_idx:%d, way:%d, tag:0x%x\n", req.line_addr, set_idx, w, tag);
                m_stats.hits++;
                m_meta[set_idx][w].last_access = ++m_tick;
                m_plru[set_idx].touch(w);
                update_lru_list_on_access(set_idx, w);
                auto data = m_data[set_idx][w];
                MemResp resp;
                resp.valid = true;
                resp.line_addr = req.line_addr;
                memcpy(resp.data, data.data(), sizeof(resp.data));
                resp.page_fault = false;
                resp.access_fault = false;
                resp.epoch = req.epoch;
                m_sched->schedule_after(m_cfg.hit_latency, [this, fut, resp, start_cycle]() mutable {
                    m_stats.total_latency += (m_sched->cycle() - start_cycle);
                    fut.set_value(resp);
                });
                return fut;
            }
        }
        LOG_LEVEL1(m_name, "Miss addr:0x%lx, set_idx:%d, tag:0x%x, epoch:%d", req.line_addr, set_idx, tag, req.epoch);

        m_stats.misses++;
        auto it = m_mshr.find(req.line_addr);
        if (it != m_mshr.end()) {
            if (it->second.epoch == req.epoch) {
                it->second.promises.push_back(fut);
                return fut;
            }
            // 同 line 不同 epoch：supersede waiter，保留 lower_in_flight 状态
            it->second.epoch = req.epoch;
            it->second.seq = ++m_mshr_seq;
            it->second.promises.clear();
            it->second.promises.push_back(fut);
            miss_seq = it->second.seq;
            schedule_lower = !it->second.lower_in_flight;
        } else {
            MSHREntry me;
            me.line_addr = req.line_addr;
            me.epoch = req.epoch;
            me.seq = ++m_mshr_seq;
            me.promises.push_back(fut);
            miss_seq = me.seq;
            m_mshr.emplace(req.line_addr, std::move(me));
            schedule_lower = true;
        }
    }

    if (schedule_lower) {
        m_sched->schedule_after(m_cfg.miss_penalty, [this, req, start_cycle, miss_seq]() {
            this->send_lower_for_miss(req, start_cycle, miss_seq);
        });
    }
    return fut;
}

void GCache::send_lower_for_miss(MemReq req, u64 start_cycle, u64 miss_seq)
{
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_mshr.find(req.line_addr);
        if (it == m_mshr.end() || it->second.seq != miss_seq) {
            return;
        }
        if (it->second.lower_in_flight) {
            return;
        }
        it->second.lower_in_flight = true;
    }

    if (m_lower) {
        auto lf = m_lower->fetch_line_async(req);
        LOG_LEVEL1(m_name, "send req addr:0x%lx, epoch:%d", req.line_addr, req.epoch);
        lf.then([this, req, start_cycle, miss_seq](const MemResp& lower_resp) {
            MemResp resp = lower_resp;
            resp.epoch = req.epoch;
            LOG_LEVEL1(m_name, "response line:0x%lx, epoch:%d", resp.line_addr, resp.epoch);
            m_sched->schedule_after(m_cfg.install_latency, [this, resp, start_cycle, miss_seq]() {
                this->complete_miss(resp.line_addr, resp, start_cycle, miss_seq);
            });
        });
    } else {
        LOG_LEVEL1(m_name, "send req addr:0x%lx to main memory, epoch:%d", req.line_addr, req.epoch);
        MemResp resp;
        resp.valid = true;
        resp.line_addr = req.line_addr;
        resp.epoch = req.epoch;
        if (m_mem && !m_mem->contains(req.line_addr, m_cfg.line_size)) {
            resp.page_fault = true;
            resp.access_fault = false;
            memset(resp.data, 0, sizeof(resp.data));
            LOG_LEVEL1(m_name, "page fault addr:0x%lx", req.line_addr);
        } else {
            auto data = this->on_lower_fetch(req.line_addr);
            memcpy(resp.data, data.data(), sizeof(resp.data));
            resp.page_fault = false;
            resp.access_fault = false;
        }
        m_sched->schedule_after(m_cfg.install_latency, [this, resp, start_cycle, miss_seq]() {
            LOG_LEVEL1(m_name, "response addr:0x%lx, epoch:%d, pf:%d", resp.line_addr, resp.epoch, resp.page_fault);
            this->complete_miss(resp.line_addr, resp, start_cycle, miss_seq);
        });
    }
}

bool GCache::try_read_hit(u64 line_addr, u64 epoch, MemResp& out) const
{
    auto [set_idx, tag] = decode(line_addr);
    std::lock_guard<std::mutex> lk(m_mtx);
    for (size_t w = 0; w < m_cfg.ways; ++w) {
        if (!m_meta[set_idx][w].valid || m_meta[set_idx][w].tag != tag) {
            continue;
        }
        out.valid = true;
        out.line_addr = line_addr;
        memcpy(out.data, m_data[set_idx][w].data(), sizeof(out.data));
        out.page_fault = false;
        out.access_fault = false;
        out.epoch = epoch;
        return true;
    }
    return false;
}

void GCache::complete_miss(u64 line_addr, MemResp resp, u64 start_cycle, u64 miss_seq)
{
    install_line(resp, false);
    apply_pending_store_merges(line_addr);

    bool fulfill_now = false;
    u64 current_epoch = 0;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_mshr.find(line_addr);
        if (it == m_mshr.end()) {
            return;
        }
        it->second.lower_in_flight = false;
        current_epoch = it->second.epoch;
        fulfill_now = (it->second.seq == miss_seq);
    }

    if (fulfill_now) {
        MemResp hit_resp;
        if (try_read_hit(line_addr, current_epoch, hit_resp)) {
            fulfill_mshr(hit_resp, start_cycle);
        } else {
            resp.epoch = current_epoch;
            fulfill_mshr(resp, start_cycle);
        }
        return;
    }

    MemResp hit_resp;
    if (try_read_hit(line_addr, current_epoch, hit_resp)) {
        fulfill_mshr(hit_resp, start_cycle);
    }
}


void GCache::update_lru_list_on_access(size_t set_idx, size_t way)
{
    auto &lst = m_lru_lists[set_idx];
    auto it = find(lst.begin(), lst.end(), way);
    if (it != lst.end()) { lst.erase(it); lst.insert(lst.begin(), way); }
}

void GCache::install_line(MemResp resp, bool mark_dirty)
{
    auto [set_idx, tag] = decode(resp.line_addr);
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
                MemResp evresp;
                evresp.line_addr = ev_addr;
                memcpy(evresp.data, evdata.data(), sizeof(evresp.data));
                evresp.valid = true;
                m_sched->schedule_after(m_cfg.writeback_latency, [this, evresp]() {
                    if (this->m_lower) {
                        this->m_lower->write_line_async(evresp);
                    }
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
        memcpy(m_data[set_idx][victim].data(), resp.data, m_data[set_idx][victim].size());
        // update replacement structures
        if (!used_plru) {
            // if PLRU not available, maintain LRU list
            update_lru_list_on_access(set_idx, victim);
        } else {
            m_plru[set_idx].touch(victim);
        }
    }
}

void GCache::fulfill_mshr(MemResp resp, u64 start_cycle)
{
    std::vector<SimpleFuture<MemResp>> prom;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_mshr.find(resp.line_addr);
        if (it != m_mshr.end()) {
            prom = move(it->second.promises);
            resp.epoch = it->second.epoch;
            m_mshr.erase(it);
        }
    }
    for (auto &f : prom) {
        m_stats.total_latency += (m_sched->cycle() - start_cycle);
        f.set_value(resp);
    }
}

void GCache::drop_mshr(u64 line_addr)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_mshr.erase(line_addr);
}

bool GCache::mshr_fulfillable(u64 line_addr, u64 seq) const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_mshr.find(line_addr);
    return it != m_mshr.end() && it->second.seq == seq;
}

// default lower fetch
std::vector<u8> GCache::on_lower_fetch(u64 line_addr)
{
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


SimpleFuture<bool> GCache::write_line_async(MemResp resp)
{
    SimpleFuture<bool> out;
    MemReq req;
    req.line_addr = resp.line_addr;
    req.epoch = resp.epoch;
    req.valid = true;
    // write-allocate: fetch or install then mark dirty
    auto f = fetch_line_async(req);
    f.then([this, resp, out](const MemResp& res){
        // overwrite and mark dirty
        m_sched->schedule_after(0, [this, resp, out]() mutable {
            this->install_line(resp, true); // mark dirty inside
            out.set_value(true);
        });
    });
    return out;
}

void GCache::merge_store_data(std::vector<u8>& line, u64 byte_addr, u64 line_addr, u64 data, u32 width)
{
    const size_t off = static_cast<size_t>(byte_addr - line_addr);
    for (u32 i = 0; i < width; ++i) {
        if (off + i < line.size()) {
            line[off + i] = static_cast<u8>((data >> (8 * i)) & 0xFFu);
        }
    }
}

void GCache::apply_pending_store_merges(u64 line_addr)
{
    std::vector<PendingStore> pending;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_mshr.find(line_addr);
        if (it == m_mshr.end() || it->second.pending_stores.empty()) {
            return;
        }
        pending = std::move(it->second.pending_stores);
        it->second.pending_stores.clear();
    }

    const auto [set_idx, tag] = decode(line_addr);
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (size_t w = 0; w < m_cfg.ways; ++w) {
            if (!m_meta[set_idx][w].valid || m_meta[set_idx][w].tag != tag) {
                continue;
            }
            for (const auto& ps : pending) {
                merge_store_data(m_data[set_idx][w], ps.req.addr, line_addr,
                                 ps.req.data, ps.req.width);
            }
            m_meta[set_idx][w].dirty = true;
            m_meta[set_idx][w].last_access = ++m_tick;
            m_plru[set_idx].touch(w);
            update_lru_list_on_access(set_idx, w);
            break;
        }
    }

    for (auto& ps : pending) {
        const u64 start_cycle = ps.start_cycle;
        m_sched->schedule_after(m_cfg.hit_latency, [this, ps, start_cycle]() mutable {
            m_stats.total_latency += (m_sched->cycle() - start_cycle);
            ps.fut.set_value(true);
        });
    }
}

SimpleFuture<bool> GCache::store_merge_async(StoreReq req)
{
    m_stats.accesses++;
    SimpleFuture<bool> fut;
    const u64 start_cycle = m_sched->cycle();
    const u64 line_addr = req.addr & ~(static_cast<u64>(m_cfg.line_size) - 1ULL);
    const auto [set_idx, tag] = decode(line_addr);
    u64 miss_seq = 0;
    bool schedule_lower = false;
    MemReq miss_req {};
    miss_req.valid = true;
    miss_req.line_addr = line_addr;
    miss_req.epoch = req.epoch;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (size_t w = 0; w < m_cfg.ways; ++w) {
            if (!m_meta[set_idx][w].valid || m_meta[set_idx][w].tag != tag) {
                continue;
            }
            LOG_LEVEL1(m_name, "Store hit addr:0x%lx line:0x%lx way:%zu", req.addr, line_addr, w);
            m_stats.hits++;
            merge_store_data(m_data[set_idx][w], req.addr, line_addr, req.data, req.width);
            m_meta[set_idx][w].dirty = true;
            m_meta[set_idx][w].last_access = ++m_tick;
            m_plru[set_idx].touch(w);
            update_lru_list_on_access(set_idx, w);
            m_sched->schedule_after(m_cfg.hit_latency, [this, fut, start_cycle]() mutable {
                m_stats.total_latency += (m_sched->cycle() - start_cycle);
                fut.set_value(true);
            });
            return fut;
        }

        LOG_LEVEL1(m_name, "Store miss addr:0x%lx line:0x%lx epoch:%lu", req.addr, line_addr, req.epoch);
        m_stats.misses++;
        PendingStore ps;
        ps.req = req;
        ps.fut = fut;
        ps.start_cycle = start_cycle;

        auto it = m_mshr.find(line_addr);
        if (it != m_mshr.end()) {
            it->second.pending_stores.push_back(std::move(ps));
            if (it->second.epoch != req.epoch) {
                it->second.epoch = req.epoch;
                it->second.seq = ++m_mshr_seq;
                miss_seq = it->second.seq;
                schedule_lower = !it->second.lower_in_flight;
            }
            return fut;
        }

        MSHREntry me;
        me.line_addr = line_addr;
        me.epoch = req.epoch;
        me.seq = ++m_mshr_seq;
        me.pending_stores.push_back(std::move(ps));
        miss_seq = me.seq;
        m_mshr.emplace(line_addr, std::move(me));
        schedule_lower = true;
    }

    if (schedule_lower) {
        m_sched->schedule_after(m_cfg.miss_penalty, [this, miss_req, start_cycle, miss_seq]() {
            this->send_lower_for_miss(miss_req, start_cycle, miss_seq);
        });
    }
    return fut;
}

// expose invalidate used by inclusive policy
void GCache::invalidate_line(u64 line_addr)
{
    auto [set_idx, tag] = decode(line_addr);
    std::lock_guard<std::mutex> lk(m_mtx);
    for (size_t w=0; w<m_cfg.ways; ++w) {
        if (m_meta[set_idx][w].valid && m_meta[set_idx][w].tag==tag) {
            if (m_meta[set_idx][w].dirty && m_lower) {
                auto data = m_data[set_idx][w];
                u64 evict_addr = reconstruct_line_addr(set_idx, m_meta[set_idx][w].tag);
                MemResp evresp;
                evresp.line_addr = evict_addr;
                memcpy(evresp.data, data.data(), sizeof(evresp.data));
                evresp.valid = true;
                m_sched->schedule_after(m_cfg.writeback_latency, [this, evresp]() {
                    if (this->m_lower) {
                        this->m_lower->write_line_async(evresp);
                    }
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

void GCache::set_name(std::string name)
{
    m_name = name;
}

void GCache::set_mem(Memory *mem)
{
    m_mem = mem;
}
