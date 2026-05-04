
#ifndef RV_TCORE_ICACHE_H
#define RV_TCORE_ICACHE_H
#include <deque>
#include <vector>

#include "rv_def.h"
#include "tlb.h"
#include "cache.h"
#include "rv_def.h"
#include "module_comm.h"

class ICache : public GCache {
public:
    ICache() = delete;
    ICache(CycleScheduler* sched, const CacheConfig& cfg, MultiLevelTLB* tlb);
    ~ICache();

public:
    virtual bool                                                request(const IFetchReq& req);
    virtual IFetchResp                                          read_line(pc_t pc);
    virtual bool                                                line_ready(pc_t pc);
    pc_t                                                        align_line(pc_t pc) const;

private:
    std::unordered_map<pc_t, SimpleFuture<MemResp>>             m_pending_futures;
    std::vector<IFetchResp>                                     m_ready;
    MultiLevelTLB*                                              m_tlb;
};

#endif