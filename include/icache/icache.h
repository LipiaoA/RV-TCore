
#ifndef RV_TCORE_ICACHE_H
#define RV_TCORE_ICACHE_H
#include <deque>
#include <vector>

#include "cache.h"
#include "rv_def.h"

static constexpr unsigned k_fetch_width = 4;     // 每包最大指令数
static constexpr unsigned k_icache_line_bytes = 64;
static constexpr unsigned k_icache_latency = 2;  // 模拟 I$ 2 cycle latency

class ICache : public CacheI {
public:
    ICache() = default;
    ICache(CycleScheduler* sched, const CacheConfig& cfg, MultiLevelTLB* tlb, InclusionPolicy pol=InclusionPolicy::NonInclusive) :
        CacheI(sched, cfg, pol), m_tlb(tlb) {}
    ~ICache() {}

public:
    void tick() {}
    bool request(pc_t line_addr) { return true;}
    bytes_t read_line(pc_t line_addr) {}

    // fetch multiple instructions starting at VA. Each future resolves to instruction bytes or empty on fault.
    vector<SimpleFuture<vector<u8>>> fetch_instructions_async(u64 vaddr, size_t inst_size, size_t fetch_count)
    static constexpr unsigned k_line_bytes = k_icache_line_bytes;

private:
    bool line_ready(pc_t line_addr) const {return false;}
    void set_latency(uint32_t latency) {}

private:
    struct pending_entry {
        pc_t line_addr;
        unsigned cycles_left;
    };
    struct ready_entry {
        pc_t line_addr;
        bytes_t line;
        ready_entry(pc_t a, bytes_t &&b) : line_addr(a), line(std::move(b)) {}
    };

    unsigned m_latency { k_icache_latency };
    std::deque<pending_entry> m_pending;
    std::vector<ready_entry> m_ready;
    MultiLevelTLB* m_tlb;
};

#endif