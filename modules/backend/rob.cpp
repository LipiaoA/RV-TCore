#include "rob/rob.h"

ROB::ROB(const ROBDeps& deps)
    : m_execute_commit_q(*deps.execute_commit_q),
      m_capacity(deps.conf.capacity)
{ }

void ROB::build() {
}

ROBID ROB::alloc(const MicroOp& inst) {
    if (full()) {
        return ROBID{ -1, 0 };
    }
    ROBID write_id = m_tail;
    RobEntry& e = m_entries[write_id.idx];
    e.valid = true;
    e.inst = inst;
    m_tail.idx = (m_tail.idx + 1) % m_capacity;
    m_tail.direct = (m_tail.idx == 0) ? 1 - m_tail.direct : m_tail.direct;
    ++m_size;
    return write_id;
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

std::optional<RobEntry> ROB::try_commit_head() {
    if (empty())
        return std::nullopt;
    RobEntry& e = m_entries[m_head.idx];
    if (!e.valid || !e.ready)
        return std::nullopt;
    RobEntry out = e;
    e.valid = false;
    m_head.idx = (m_head.idx + 1) % m_capacity;
    m_head.direct = (m_head.idx == 0) ? 1 - m_head.direct : m_head.direct;
    --m_size;
    return out;
}

bool ROB::full() const {
    return m_size >= m_capacity;
}

bool ROB::empty() const {
    return m_size == 0;
}

bool ROB::is_older_than(const ROBID& a, const ROBID& b) const
{
    u32 a_idx = (a.direct == m_tail.direct) ? a.idx : a.idx + m_capacity;
    u32 b_idx = (b.direct == m_tail.direct) ? b.idx : b.idx + m_capacity;
    return a_idx < b_idx;
}

void ROB::tick() {
    ExecResult res;
    while (m_execute_commit_q.read(res)) {
        if (!res.valid || res.rob_id < 0)
            continue;
        mark_ready(ROBID{static_cast<u32>(res.rob_id), 0}, res.result);
    }
}

void ROB::commit() {
    // 顺序提交在 CommitUnit 中驱动，ROB 仅提供 try_commit_head
}
