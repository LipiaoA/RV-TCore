#include "flush_unit.h"

FlushUnit::FlushUnit(const FlushUnitConfig& conf, const FlushUnitDeps& deps)
    : m_width(conf.width)
    , m_flush_event(*deps.flush_event)
    , m_flush_req_q(*deps.flush_req_q)
{
}

void FlushUnit::tick()
{
    while (true) {
        FlushEvent event;
        bool ret = m_flush_req_q.read(event);
        if (!ret)
            break;
    }
}

void FlushUnit::commit()
{
}

void FlushUnit::build()
{
}

void FlushUnit::dump_state() const
{
}

FlushUnit::~FlushUnit()
{
}
