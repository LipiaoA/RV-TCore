#include <bit>

#include "util.h"
#include "icache.h"
#include "log.h"


ICache::ICache(CycleScheduler* sched, const CacheConfig& cfg, MultiLevelTLB* tlb) :
    GCache(sched, cfg), m_tlb(tlb) {}

ICache::~ICache() {}

bool ICache::request(pc_t pc)
{
    pc_t line_addr = align_line(pc);
    if (m_pending_futures.find(line_addr) != m_pending_futures.end()) {
        return true;
    }
    auto fut = fetch_line_async(line_addr);
    fut.then([this, line_addr](const std::vector<u8>& line) {
        ready_entry entry(line_addr, line);
        m_ready.emplace_back(std::move(entry));
    });
    m_pending_futures.insert({line_addr, std::move(fut)});
    return true;
}

bytes_t ICache::read_line(pc_t line_addr)
{
    for (auto &entry : m_ready) {
        if (entry.line_addr == line_addr) {
            bytes_t line = entry.line;
            m_ready.erase(std::find(m_ready.begin(), m_ready.end(), entry));
            m_pending_futures.erase(line_addr);
            return line;
        }
    }
    return bytes_t();
}

bool ICache::line_ready(pc_t line_addr)
{
    auto it = m_pending_futures.find(line_addr);
    if (it != m_pending_futures.end()) {
        return it->second.ready();
    }
    return false;
}

pc_t ICache::align_line(pc_t pc) const
{
    return (pc / m_cfg.line_size) * m_cfg.line_size;
}