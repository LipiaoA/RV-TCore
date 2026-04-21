
#ifndef RV_TCORE_ICACHE_H
#define RV_TCORE_ICACHE_H
#include <deque>
#include <vector>

#include "rv_def.h"
#include "tlb.h"
#include "cache.h"
#include "rv_def.h"


class ICache : public GCache {
public:
    ICache() = delete;
    ICache(CycleScheduler* sched, const CacheConfig& cfg, MultiLevelTLB* tlb);
    ~ICache();

public:
    virtual bool                                                request(pc_t pc);
    virtual bytes_t                                             read_line(pc_t line_addr);
    virtual bool                                                line_ready(pc_t line_addr);
    pc_t                                                        align_line(pc_t pc) const;

private:
    struct ready_entry {
        pc_t        line_addr;
        bytes_t     line;
        bool operator==(const ready_entry &other) const {
            return line_addr == other.line_addr;
        }
        ready_entry(pc_t a, bytes_t b)
        : line_addr(a),
          line(std::move(b)) {}
    };

    std::unordered_map<pc_t, SimpleFuture<bytes_t>>             m_pending_futures;
    std::vector<ready_entry>                                    m_ready;
    MultiLevelTLB*                                              m_tlb;
};

#endif