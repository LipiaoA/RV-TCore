#ifndef RV_TCORE_DCACHE_H
#define RV_TCORE_DCACHE_H

#include "cache.h"

class DCache : public GCache {
public:
    DCache(CycleScheduler *sched, const CacheConfig &cfg, InclusionPolicy pol = InclusionPolicy::NonInclusive)
    : GCache(sched, cfg, pol) {}
    virtual ~DCache() {}

};

#endif
