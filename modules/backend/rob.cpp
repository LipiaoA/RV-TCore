#include "rob/rob.h"

#include "perf/perf_def.h"

ROB::ROB(const ROBConfig& conf, const ROBDeps& deps)
    : m_capacity(conf.capacity)
    , m_stats(deps.perf_stats)
{
    m_head = ROBID{ 0, 0 };
    m_tail = ROBID{ 0, 0 };
    m_size = 0;
    assert(m_capacity > 0);
    std::fill(m_entries, m_entries + MAX_ROB_ENTRIES, RobEntry());
}

void ROB::account_flushed(RobEntry& entry, FlushType type)
{
    if (!m_stats || !m_stats->perf() || !entry.valid || !entry.inst) {
        return;
    }
    MInstPtr inst = entry.inst;
    if (inst->perf_accounted || inst->perf_issued_uops == 0) {
        return;
    }
    const u64 n = inst->perf_issued_uops;
    m_stats->issue.flushed_uops += n;
    const size_t typ = static_cast<size_t>(type);
    if (typ < PerfStats::MAX_FLUSH_COUNT) {
        m_stats->issue.flushed_uops_by_type[typ] += n;
    }
    inst->perf_accounted = true;
}

void ROB::flush(const FlushEvent& event) {
    ROBID cur, cut;
    switch (event.scope) {
    case FlushScope::ALL: {
        for_each([this, &event](RobEntry& e) {
            account_flushed(e, event.type);
        });
        reset();
        return;
    } break;
    case FlushScope::YOUNGER_THAN: {
        cut = next_id(event.rid);
    } break;
    case FlushScope::FROM_RID: {
        cut = event.rid;
    } break;
    default:
        assert(0);
    }
    cur = cut;
    while (!(cur.idx == m_tail.idx && cur.direct == m_tail.direct)) {
        account_flushed(m_entries[cur.idx], event.type);
        m_entries[cur.idx].valid = false;
        cur = next_id(cur);
        --m_size;
    }
    m_tail = cut;
}

void ROB::reset() {
    m_head = ROBID{ 0, 0 };
    m_tail = ROBID{ 0, 0 };
    m_size = 0;
    assert(m_capacity > 0);
    std::fill(m_entries, m_entries + MAX_ROB_ENTRIES, RobEntry());
}

std::optional<ROBID> ROB::alloc(const MInstPtr inst) {
    if (full()) {
        return std::nullopt;
    }
    ROBID write_id = m_tail;
    RobEntry& e = m_entries[write_id.idx];
    e.valid = true;
    e.inst = inst;
    m_tail.idx = (m_tail.idx + 1) % m_capacity;
    m_tail.direct = (m_tail.idx == 0) ? 1 - m_tail.direct : m_tail.direct;
    ++m_size;
    // LOG_LEVEL1("ROB", "Allocate pc:0x%lx, inst: %s, fetch id: %lu, rid:%s",
    //     inst->pc, inst->inst_name.c_str(), inst->fetch_id, write_id.str());
    LOG_LEVEL1("ROB", "ROB Allocated rid:%s, head:%s, tail:%s, fetch id: %lu",
        write_id.str(), m_head.str(), m_tail.str(), inst->fetch_id);
    return std::make_optional<ROBID>(write_id);
}

RobEntry* ROB::get(ROBID rid) {
    if (rid.idx >= m_capacity)
        return nullptr;
    if (!m_entries[rid.idx].valid)
        return nullptr;
    return &m_entries[rid.idx];
}

void ROB::mark_ready(ROBID rid, u64 result_val) {
    RobEntry* e = get(rid);
    if (e) {
        e->ready = true;
        e->result = result_val;
    }
}

void ROB::mark_not_ready(ROBID rid) {
    RobEntry* e = get(rid);
    if (e) {
        e->ready = false;
    }
}

void ROB::for_each(const std::function<void(RobEntry&)>& fn)
{
    ROBID cur = m_head;
    for (size_t n = 0; n < m_size; ++n) {
        RobEntry& e = m_entries[cur.idx];
        if (e.valid) {
            fn(e);
        }
        cur = next_id(cur);
    }
}

void ROB::for_each(const std::function<void(const RobEntry&)>& fn) const
{
    ROBID cur = m_head;
    for (size_t n = 0; n < m_size; ++n) {
        const RobEntry& e = m_entries[cur.idx];
        if (e.valid) {
            fn(e);
        }
        cur = next_id(cur);
    }
}

std::optional<RobEntry> ROB::commit_head() {
    if (empty())
        return std::nullopt;
    RobEntry& e = m_entries[m_head.idx];
    if (!e.valid || e.inst->state != InstState::RETIRED)
        return std::nullopt;
    RobEntry out = e;
    e.valid = false;
    m_head.idx = (m_head.idx + 1) % m_capacity;
    m_head.direct = (m_head.idx == 0) ? 1 - m_head.direct : m_head.direct;
    --m_size;
    return out;
}

std::optional<RobEntry> ROB::head() {
    if (empty())
        return std::nullopt;
    RobEntry& e = m_entries[m_head.idx];
    return std::make_optional<RobEntry>(e);
}

bool ROB::full() const {
    return m_size >= m_capacity;
}

bool ROB::has_space(u32 count) const {
    return m_size + count <= m_capacity;
}

bool ROB::empty() const {
    return m_size == 0;
}

bool ROB::is_older_than(const ROBID& a, const ROBID& b) const
{
    u32 a_idx = (a.direct == m_head.direct) ? a.idx : a.idx + m_capacity;
    u32 b_idx = (b.direct == m_head.direct) ? b.idx : b.idx + m_capacity;
    return a_idx < b_idx;
}

ROBID ROB::next_id(ROBID id) const
{
    id.idx = (id.idx + 1) % m_capacity;
    if (id.idx == 0)
        id.direct = 1 - id.direct;
    return id;
}
