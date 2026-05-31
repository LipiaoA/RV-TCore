#ifndef RV_TCORE_FLUSH_UNIT_H
#define RV_TCORE_FLUSH_UNIT_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

class FlushUnit {
public:
    FlushUnit(const FlushUnitConfig& conf, const FlushUnitDeps& deps);
    ~FlushUnit();

    void                                    tick();
    void                                    commit();
    void                                    build();
    void                                    reset();
    void                                    dump_state() const;

private:
    u32                                     m_width;
    FlushEvent&                             m_flush_event;
    ASyncQueue<FlushEvent>&                 m_flush_req_q;
    PerfStats*                              m_stats { nullptr };
};

#endif
