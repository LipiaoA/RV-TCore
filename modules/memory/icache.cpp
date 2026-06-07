#include <bit>
#include <algorithm>

#include "util.h"
#include "icache.h"
#include "log.h"


ICache::ICache(CycleScheduler* sched, const CacheConfig& cfg, MultiLevelTLB* tlb) :
    GCache(sched, cfg), m_tlb(tlb) {}


ICache::~ICache() {}

void ICache::drop_ready_line(pc_t line_addr)
{
    m_ready.erase(
        std::remove_if(m_ready.begin(), m_ready.end(),
                       [line_addr](const IFetchResp& e) {
                           return e.line_addr == line_addr;
                       }),
        m_ready.end());
}

bool ICache::request(const IFetchReq& ireq)
{
    pc_t line_addr = align_line(ireq.pc);

    auto it = m_pending_futures.find(line_addr);
    if (it != m_pending_futures.end()) {
        if (it->second.epoch == ireq.epoch) {
            // 同 line、同 epoch：合并为一次 in-flight 请求
            return true;
        }
        // 同 line、不同 epoch：flush/redirect 后的新取指，丢弃旧 ICache 侧 pending
        m_pending_futures.erase(it);
        drop_ready_line(line_addr);
    }

    MemReq req;
    req.valid = true;
    req.line_addr = line_addr;
    req.epoch = ireq.epoch;
    req.write = false;

    const u64 req_epoch = ireq.epoch;
    auto fut = fetch_line_async(req);
    fut.then([this, line_addr, req_epoch](const MemResp& resp) {
        auto pit = m_pending_futures.find(line_addr);
        if (pit == m_pending_futures.end() || pit->second.epoch != req_epoch) {
            return;
        }
        if (resp.epoch != req_epoch) {
            return;
        }

        IFetchResp entry;
        entry.valid = true;
        entry.line_addr = resp.line_addr;
        memcpy(entry.data, resp.data, sizeof(entry.data));
        entry.page_fault = resp.page_fault;
        entry.access_fault = resp.access_fault;
        entry.epoch = resp.epoch;
        m_ready.emplace_back(std::move(entry));
        LOG_LEVEL1("ICache", "line ready:0x%lx, addr:0x%lx, data:0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx",
                    line_addr, line_addr, resp.data[0], resp.data[1], resp.data[2], resp.data[3],
                    resp.data[4], resp.data[5], resp.data[6], resp.data[7]);
    });

    m_pending_futures[line_addr] = PendingLineFetch{req_epoch, fut};
    return true;
}

IFetchResp ICache::read_line(pc_t pc)
{
    pc_t line_addr = align_line(pc);
    auto it = m_ready.begin();
    for (; it != m_ready.end(); ++it) {
        if (it->line_addr == line_addr) {
            IFetchResp res = *it;
            m_ready.erase(it);
            m_pending_futures.erase(line_addr);
            return res;
        }
    }
    return IFetchResp{false, 0, {}, false, false, 0};
}

bool ICache::line_ready(pc_t pc)
{
    pc_t line_addr = align_line(pc);

    for (const auto& entry : m_ready) {
        if (entry.line_addr == line_addr) {
            return true;
        }
    }

    auto it = m_pending_futures.find(line_addr);
    if (it != m_pending_futures.end()) {
        return it->second.fut.ready();
    }
    return false;
}

pc_t ICache::align_line(pc_t pc) const
{
    return (pc / m_cfg.line_size) * m_cfg.line_size;
}
