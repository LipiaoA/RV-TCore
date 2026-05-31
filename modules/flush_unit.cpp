#include "flush_unit.h"
#include "perf/perf_stats.h"

FlushUnit::FlushUnit(const FlushUnitConfig& conf, const FlushUnitDeps& deps)
    : m_width(conf.width)
    , m_flush_event(*deps.flush_event)
    , m_flush_req_q(*deps.flush_req_q)
    , m_stats(deps.perf_stats)
{
}

struct FlushTypeHash {
    std::size_t operator()(FlushType type) const noexcept
    {
        return std::hash<int>{}(static_cast<int>(type));
    }
};

void FlushUnit::tick()
{
    static const std::unordered_map<FlushType, int, FlushTypeHash> priority = {
        {FlushType::MACHINE_CHECK,  100},
        {FlushType::DEBUG,          90},
        {FlushType::EXCEPTION,      80},
        {FlushType::INTERRUPT,      70},
        {FlushType::BRANCH_MISPRED, 60},
        {FlushType::MEMORY_ORDER,   50},
        {FlushType::LOAD_REPLAY,    40},
        {FlushType::FENCE,          30},
    };
    FlushEvent event;
    int pri = -1;
    m_flush_event.valid = false;

    while (!m_flush_req_q.empty()) {
        bool ret = m_flush_req_q.read(event);
        if (!ret || !event.valid) {
            continue;
        }
        if (m_stats) {
            ++m_stats->flush.requests;
        }
        auto it = priority.find(event.type);
        assert (it != priority.end());
        if (pri < it->second) {
            pri = it->second;
            m_flush_event = event;
        }
    }
    if (m_flush_event.valid && m_stats) {
        ++m_stats->flush.events;
        ++m_stats->flush.by_type[static_cast<size_t>(m_flush_event.type)];
    }
}

void FlushUnit::commit()
{
    m_flush_req_q.commit();
}

void FlushUnit::build()
{
    m_flush_event.valid = false;
    m_flush_req_q.clear();
}

void FlushUnit::reset()
{
    m_flush_event.valid = false;
    m_flush_req_q.clear();
}

void FlushUnit::dump_state() const
{
}

FlushUnit::~FlushUnit()
{
}
