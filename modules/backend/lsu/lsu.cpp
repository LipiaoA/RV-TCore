#include <string>
#include <utility>
#include <algorithm>
#include <cstring>

#include "lsu.h"

u32 LSU::lsu_idx = 0;

namespace {

bool mask_bit_set(const uint64_t mask[2], size_t idx)
{
    if (idx < 64) {
        return (mask[0] >> idx) & 1ULL;
    }
    return (mask[1] >> (idx - 64)) & 1ULL;
}

void mask_bit_set(uint64_t mask[2], size_t idx)
{
    if (idx < 64) {
        mask[0] |= (1ULL << idx);
    } else {
        mask[1] |= (1ULL << (idx - 64));
    }
}

void mask_bit_clear(uint64_t mask[2], size_t idx)
{
    if (idx < 64) {
        mask[0] &= ~(1ULL << idx);
    } else {
        mask[1] &= ~(1ULL << (idx - 64));
    }
}

bool same_rob_id(const ROBID& lhs, const ROBID& rhs)
{
    return lhs.idx == rhs.idx && lhs.direct == rhs.direct;
}

} // namespace

LSU::LSU(const LSUConfig& conf, const LSUDeps& deps)
    : m_complete_q(conf.load_q_size + conf.store_q_size),
      m_rob(*deps.rob),
      m_dcache(*deps.dcache),
      m_dtlb(*deps.dtlb),
      m_mem(*deps.mem),
      m_sched(*deps.sched),
      m_flush_req_q(*deps.flush_req_q),
      m_wakeup_bus(*deps.wakeup_bus),
      lsu_ld_q(*deps.lsu_ld_q),
      lsu_st_q(*deps.lsu_st_q),
      m_conf(conf),
      m_issue_width(conf.issue_width),
      m_load_q_size(conf.load_q_size),
      m_store_q_size(conf.store_q_size)
{
    m_idx = LSU::lsu_idx++;
    m_ld_id = 0;
    m_st_id = 0;
}

void LSU::build()
{
    m_complete_q.clear();
    lsu_ld_q.assign(m_load_q_size, LQEntry {});
    lsu_st_q.assign(m_store_q_size, SQEntry {});
    m_block_load_mask.assign(m_load_q_size, false);
    m_p1_block_load_mask.assign(m_load_q_size, false);
    m_exe_slots.assign(m_issue_width, LSUExeSlot {});
    m_s1_slots.assign(m_issue_width, LSUS1Slot {});
    m_ld_id = 0;
    m_st_id = 0;
    m_ldq_head = m_ldq_tail = 0;
    m_stq_head = m_stq_tail = m_stq_commit_head = m_stq_execute_head = 0;
    m_live_store_mask[0] = m_live_store_mask[1] = 0;
}

LSU::~LSU()
{
    lsu_ld_q.clear();
    lsu_st_q.clear();
}

size_t LSU::wrap_inc(size_t idx, size_t cap)
{
    return (idx + 1) % cap;
}

bool LSU::st_dep_test(const LQEntry& e, size_t stq_idx)
{
    return mask_bit_set(e.st_dep_mask, stq_idx);
}

void LSU::st_dep_set(uint64_t mask[2], size_t stq_idx)
{
    mask_bit_set(mask, stq_idx);
}

void LSU::st_dep_clear(uint64_t mask[2], size_t stq_idx)
{
    mask_bit_clear(mask, stq_idx);
}

u8 LSU::gen_byte_mask(u64 addr, u32 width)
{
    const u32 shift = static_cast<u32>(addr & 7ULL);
    switch (width) {
    case 1:
        return static_cast<u8>(1u << shift);
    case 2:
        return static_cast<u8>(3u << (shift & ~1u));
    case 4:
        return static_cast<u8>((shift >= 4) ? 0xF0u : 0x0Fu);
    case 8:
    default:
        return 0xFFu;
    }
}

bool LSU::dword_addr_match(u64 a, u64 b)
{
    return (a >> 3) == (b >> 3);
}

bool LSU::mask_overlap(u8 a, u8 b)
{
    return (a & b) != 0;
}

bool LSU::mask_contains(u8 container, u8 sub)
{
    return (container & sub) == sub;
}

u64 LSU::apply_load_gen(u64 store_data, u64 addr, u32 width)
{
    const u32 shift = static_cast<u32>((addr & 7ULL) * 8);
    u64 val = store_data >> shift;
    switch (width) {
    case 1:
        return val & 0xFFu;
    case 2:
        return val & 0xFFFFu;
    case 4:
        return val & 0xFFFFFFFFu;
    default:
        return val;
    }
}

bool LSU::ldq_full() const
{
    return wrap_inc(m_ldq_tail, m_load_q_size) == m_ldq_head;
}

bool LSU::stq_full() const
{
    return wrap_inc(m_stq_tail, m_store_q_size) == m_stq_head;
}

void LSU::update_live_store_mask()
{
    m_live_store_mask[0] = 0;
    m_live_store_mask[1] = 0;
    for (size_t i = 0; i < m_store_q_size; ++i) {
        if (lsu_st_q[i].valid) {
            mask_bit_set(m_live_store_mask, i);
        }
    }
}

void LSU::tick()
{
    m_stall = NO_STALL;
    m_mem_port_busy = false;
    m_tlb_port_busy = false;

    w1();
    drain_writebacks();
    for (auto& s : lsu_st_q) {
        if (s.valid && s.addr_ready && !s.addr_is_virtual && !s.sta_completed) {
            try_complete_sta(s);
        }
    }
    s1();
    w0();
    exe_s0();
    s0();
    p0();

    if (!m_block_load_mask.empty()) {
        m_p1_block_load_mask = m_block_load_mask;
        std::fill(m_block_load_mask.begin(), m_block_load_mask.end(), false);
    }
}

void LSU::commit()
{
}

void LSU::drain_writebacks()
{
    while (!m_wb_pending.empty()) {
        MInstPtr inst = m_wb_pending.front();
        m_wb_pending.pop_front();
        if (inst && inst->valid) {
            m_complete_q.write(inst);
        }
    }
}

void LSU::w1()
{
    // process wb stage 1
    std::deque<LSUWBSlot> remaining;
    for (auto& slot : m_wb_pipe) {
        if (!slot.valid) {
            continue;
        }
        if (slot.cycles_left > 0) {
            --slot.cycles_left;
            remaining.push_back(slot);
        } else {
            complete_load(slot.load_id, slot.data);
        }
    }
    m_wb_pipe = std::move(remaining);
}

void LSU::w0()
{
    // process pending mem responses
    while (!m_pending_mem_resps.empty()) {
        PendingMemResp pending = m_pending_mem_resps.front();
        m_pending_mem_resps.pop_front();

        auto* entry_ptr = find_load_by_id(pending.load_id);
        if (!entry_ptr || !entry_ptr->valid || !entry_ptr->in_flight
            || entry_ptr->mem_req_gen != pending.req_gen) {
            continue;
        }
        auto& entry = *entry_ptr;
        u64 data = 0;
        if (pending.resp.valid) {
            data = extract_load_data(pending.resp, pending.byte_addr, pending.width,
                                     m_conf.dcache_line_size);
        }
        const u8 load_mask = entry.uop ? gen_byte_mask(pending.byte_addr, entry.uop->ls_width) : 0xFFu;
        auto fwd = try_forward(pending.byte_addr, load_mask, entry, true);
        if (fwd.has_value()) {
            data = fwd->second;
        }
        enqueue_wb(pending.load_id, data, 1);
        entry.in_flight = false;
        entry.executed = true;
    }
}

void LSU::enqueue_wb(uint64_t load_id, u64 data, u32 delay)
{
    LSUWBSlot slot {};
    slot.valid = true;
    slot.load_id = load_id;
    slot.data = data;
    slot.cycles_left = delay;
    m_wb_pipe.push_back(slot);
}

void LSU::s1()
{
    // process s1 lcam
    for (auto& slot : m_s1_slots) {
        if (!slot.valid) {
            continue;
        }
        do_s1_lcam(slot);
        slot.valid = false;
    }
}

void LSU::exe_s0()
{
    // process execute/s0 stage: TLB lookup and DCache request issue
    for (auto& slot : m_exe_slots) {
        if (!slot.valid) {
            continue;
        }
        do_exe_s0_load(slot);
        slot.valid = false;
    }
}

void LSU::do_exe_s0_load(LSUExeSlot& slot)
{
    auto* entry = find_load_by_ldq_idx(slot.ldq_idx);
    if (!entry || !entry->valid) {
        return;
    }

    slot.tlb_miss = false;

    if (m_tlb_port_busy) {
        m_stall = TLB_PORT_FULL;
        entry->state = WAIT_TLB;
        return;
    }
    m_tlb_port_busy = true;

    auto [ok, pa] = m_dtlb.lookup_a(slot.vaddr);
    if (!ok) {
        slot.tlb_miss = true;
        entry->addr_is_virtual = true;
        entry->state = WAIT_TLB;
        return;
    }
    slot.paddr = pa;
    entry->pa = pa;
    entry->addr_is_virtual = false;

    if (!try_arbitrate_mem_port()) {
        m_stall = DCACHE_PORT_FULL;
        entry->state = WAIT_CACHE;
        return;
    }

    issue_mem_access(*entry, pa, slot.byte_mask);
    slot.mem_issued = true;

    LSUS1Slot s1_slot {};
    s1_slot.valid = true;
    s1_slot.ldq_idx = slot.ldq_idx;
    s1_slot.vaddr = slot.vaddr;
    s1_slot.paddr = pa;
    s1_slot.byte_mask = slot.byte_mask;
    s1_slot.mem_issued = slot.mem_issued;
    for (auto& s : m_s1_slots) {
        if (!s.valid) {
            s = s1_slot;
            return;
        }
    }
    m_stall = DCACHE_PORT_FULL;
}

void LSU::do_s1_lcam(LSUS1Slot& slot)
{
    auto* entry = find_load_by_ldq_idx(slot.ldq_idx);
    if (!entry || !entry->valid) {
        return;
    }

    slot.killed = false;
    slot.forward_val = false;

    const u8 load_mask = slot.byte_mask;
    auto fwd = try_forward(slot.vaddr, load_mask, *entry, true);
    if (fwd.has_value()) {
        slot.forward_val = true;
        slot.forward_stq_idx = fwd->first;
        slot.killed = true;
        entry->forward_std_val = true;
        entry->forward_stq_idx = fwd->first;
        entry->speculative = false;
        entry->in_flight = false;
        entry->executed = true;
        entry->state = READY;
        enqueue_wb(entry->load_id, fwd->second, 1);
        if (entry->uop && entry->uop->rd.valid) {
            wakeup_load(entry->uop, true);
        }
        return;
    }

    if (checkViolation(slot.vaddr, entry->rob_idx, load_mask)
        || has_stq_conflict(slot.vaddr, load_mask, *entry)) {
        slot.killed = true;
        replay(*entry);
        return;
    }

    if (slot.mem_issued) {
        entry->executed = true;
    }
}

void LSU::issue_mem_access(LQEntry& entry, u64 pa, u8 byte_mask)
{
    (void)byte_mask;
    ++entry.mem_req_gen;
    entry.in_flight = true;
    entry.state = WAIT_CACHE;
    entry.speculative = has_unknown_older_store(entry.addr,
        entry.uop ? gen_byte_mask(entry.addr, entry.uop->ls_width) : 0xFFu,
        entry.rob_idx);

    const uint64_t load_id = entry.load_id;
    const uint64_t req_gen = entry.mem_req_gen;
    const u64 byte_addr = entry.addr;
    const u32 width = entry.uop ? entry.uop->ls_width : 8;
    const u32 line_size = m_conf.dcache_line_size;

    MemReq req {};
    req.valid = true;
    req.line_addr = round_down(pa, line_size);
    req.epoch = entry.uop ? entry.uop->epoch : 0;

    m_dcache.fetch_line_async(req).then([this, load_id, req_gen, byte_addr, width](const MemResp& resp) {
        PendingMemResp pending {};
        pending.load_id = load_id;
        pending.req_gen = req_gen;
        pending.byte_addr = byte_addr;
        pending.width = width;
        pending.resp = resp;
        m_pending_mem_resps.push_back(pending);
    });
}

void LSU::s0()
{
    // process store tlb
    if (m_pending_sta_tlb.empty()) {
        return;
    }
    if (m_tlb_port_busy) {
        return;
    }

    size_t stq_idx = m_pending_sta_tlb.front();
    m_pending_sta_tlb.pop_front();
    auto* st = find_store_by_stq_idx(stq_idx);
    if (!st || !st->valid || !st->addr_ready) {
        return;
    }

    m_tlb_port_busy = true;
    auto [ok, pa] = m_dtlb.lookup_a(st->addr);
    if (!ok) {
        st->addr_is_virtual = true;
        m_pending_sta_tlb.push_back(stq_idx);
        return;
    }
    st->pa = pa;
    st->addr_is_virtual = false;
    //FIXME: Flush Type should be LOAD_REPLAY
    detect_store_violations(*st);
}

void LSU::p0()
{
    // schedule operations
    std::vector<size_t> ready_loads;
    ready_loads.reserve(m_load_q_size);
    for (size_t i = 0; i < m_load_q_size; ++i) {
        auto& entry = lsu_ld_q[i];
        if (!entry.valid || !entry.addr_ready || entry.in_flight || entry.executed
            || entry.succeeded || entry.order_fail) {
            continue;
        }
        if (entry.state != WAIT_ADDR && entry.state != WAIT_SQ_CHECK
            && entry.state != WAIT_TLB && entry.state != WAIT_CACHE) {
            continue;
        }
        if (is_load_blocked(i)) {
            continue;
        }
        ready_loads.push_back(i);
    }

    std::stable_sort(ready_loads.begin(), ready_loads.end(),
        [this](size_t a, size_t b) {
            return m_rob.is_older_than(lsu_ld_q[a].rob_idx, lsu_ld_q[b].rob_idx);
        });

    u32 issued = 0;
    for (size_t idx : ready_loads) {
        if (issued >= m_issue_width) {
            break;
        }
        auto& entry = lsu_ld_q[idx];
        if (should_wait_for_store_set(entry)) {
            continue;
        }
        if (entry.waiting_on_store) {
            auto* st = find_store_by_id(entry.wait_store_id);
            if (st && (!st->addr_ready || !st->data_ready)) {
                continue;
            }
            entry.waiting_on_store = false;
            entry.wait_store_id = static_cast<uint64_t>(-1);
        }

        const u8 load_mask = entry.uop ? gen_byte_mask(entry.addr, entry.uop->ls_width) : 0xFFu;
        if (checkViolation(entry.addr, entry.rob_idx, load_mask)
            || has_stq_conflict(entry.addr, load_mask, entry)) {
            entry.state = WAIT_SQ_CHECK;
            entry.waiting_on_store = true;
            continue;
        }

        fire_s0_load(idx);
        mark_load_blocked(idx);
        ++issued;
    }

    try_store_commit();
    dequeue_succeeded_stores();
}

void LSU::fire_s0_load(size_t ldq_idx)
{
    auto& entry = lsu_ld_q[ldq_idx];
    const u8 load_mask = entry.uop ? gen_byte_mask(entry.addr, entry.uop->ls_width) : 0xFFu;

    LSUExeSlot slot {};
    slot.valid = true;
    slot.ldq_idx = ldq_idx;
    slot.vaddr = entry.addr;
    slot.byte_mask = load_mask;
    entry.state = WAIT_TLB;

    for (auto& s : m_exe_slots) {
        if (!s.valid) {
            s = slot;
            if (entry.uop && entry.uop->rd.valid) {
                wakeup_load(entry.uop, true);
            }
            return;
        }
    }
    m_stall = TLB_PORT_FULL;
}

bool LSU::try_arbitrate_mem_port()
{
    if (m_mem_port_busy) {
        return false;
    }
    m_mem_port_busy = true;
    return true;
}

void LSU::release_mem_port()
{
    m_mem_port_busy = false;
}

void LSU::dequeue_succeeded_stores()
{
    while (lsu_st_q[m_stq_head].valid && lsu_st_q[m_stq_head].committed
           && lsu_st_q[m_stq_head].succeeded) {
        mask_bit_clear(m_live_store_mask, m_stq_head);
        for (size_t i = 0; i < m_load_q_size; ++i) {
            st_dep_clear(lsu_ld_q[i].st_dep_mask, m_stq_head);
        }
        lsu_st_q[m_stq_head] = SQEntry {};
        m_stq_head = wrap_inc(m_stq_head, m_store_q_size);
    }
}

void LSU::try_store_commit()
{
    if (m_mem_port_busy) {
        return;
    }
    for (size_t n = 0; n < m_store_q_size; ++n) {
        size_t idx = (m_stq_execute_head + n) % m_store_q_size;
        auto& s = lsu_st_q[idx];
        if (!s.valid || !s.committed || s.succeeded) {
            continue;
        }
        if (!s.addr_ready || !s.data_ready || s.addr_is_virtual) {
            return;
        }
        if (!try_arbitrate_mem_port()) {
            m_stall = DCACHE_PORT_FULL;
            return;
        }
        write_memory(s.pa ? s.pa : s.addr, s.data, s.ls_width ? s.ls_width : 8);
        update_cache_line(s.pa ? s.pa : s.addr);
        s.succeeded = true;
        m_stq_execute_head = wrap_inc(m_stq_execute_head, m_store_q_size);
        release_mem_port();
        return;
    }
}

void LSU::complete_load(uint64_t load_id, uint64_t data)
{
    auto* entry_ptr = find_load_by_id(load_id);
    if (!entry_ptr || !entry_ptr->valid) {
        return;
    }
    auto& entry = *entry_ptr;

    const u8 load_mask = entry.uop ? gen_byte_mask(entry.addr, entry.uop->ls_width) : 0xFFu;
    if (!entry.forward_std_val && checkViolation(entry.addr, entry.rob_idx, load_mask)) {
        replay(entry);
        if (entry.uop) {
            for (const auto& s : lsu_st_q) {
                if (s.valid && s.uop && m_rob.is_older_than(s.rob_idx, entry.rob_idx)
                    && s.addr_ready && dword_addr_match(s.addr, entry.addr)
                    && mask_overlap(gen_byte_mask(s.addr, s.ls_width), load_mask)
                    && !s.data_ready) {
                    m_ssp.mergeSSID(entry.uop->pc, s.uop->pc);
                    break;
                }
            }
        }
        return;
    }

    entry.in_flight = false;
    entry.executed = true;
    entry.succeeded = true;
    entry.value_ready = true;
    entry.data = data;
    entry.state = READY;

    if (entry.uop) {
        entry.uop->rd.val = data;
        entry.uop->load_data = data;
        wakeup_load(entry.uop, false);
        m_wb_pending.push_back(entry.uop);
    }
}

void LSU::try_complete_sta(SQEntry& st)
{
    if (!st.uop || st.sta_completed) {
        return;
    }
    st.state = READY;
    st.sta_completed = true;
    m_complete_q.write(st.uop);
}

void LSU::wakeup_load(MInstPtr inst, bool speculative)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
    WakeupEvent event {};
    event.valid = true;
    event.type = speculative ? WAKEUP_BYPASS : WAKEUP_BYPASS;
    event.ptag = inst->rd.ptag;
    event.rid = inst->rid;
    m_wakeup_bus.push_back(event);
}

std::optional<std::pair<size_t, u64>> LSU::try_forward(u64 addr, u8 load_mask,
                                                      const LQEntry& load, bool can_forward) const
{
    if (!can_forward) {
        return std::nullopt;
    }
    const size_t fwd_idx = pick_forwarding_stq(addr, load_mask, load);
    if (fwd_idx >= m_store_q_size) {
        return std::nullopt;
    }
    const auto& s = lsu_st_q[fwd_idx];
    if (!s.data_ready) {
        return std::nullopt;
    }
    const u8 store_mask = gen_byte_mask(s.addr, s.ls_width ? s.ls_width : 8);
    if (!mask_contains(store_mask, load_mask)) {
        return std::nullopt;
    }
    const u64 data = apply_load_gen(s.data, addr, load.uop ? load.uop->ls_width : 8);
    return std::make_pair(fwd_idx, data);
}

size_t LSU::pick_forwarding_stq(u64 addr, u8 load_mask, const LQEntry& load) const
{
    size_t found = m_store_q_size;
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid || !st_dep_test(load, i)) {
            continue;
        }
        if (!m_rob.is_older_than(s.rob_idx, load.rob_idx)) {
            continue;
        }
        if (!s.addr_ready || s.addr_is_virtual) {
            continue;
        }
        if (!dword_addr_match(s.addr, addr)) {
            continue;
        }
        const u8 store_mask = gen_byte_mask(s.addr, s.ls_width ? s.ls_width : 8);
        if (!mask_overlap(store_mask, load_mask)) {
            continue;
        }
        if (!mask_contains(store_mask, load_mask)) {
            continue;
        }
        if (i >= load.youngest_stq_idx) {
            continue;
        }
        found = i;
    }
    return found;
}

bool LSU::has_stq_conflict(u64 addr, u8 load_mask, const LQEntry& load) const
{
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid || !st_dep_test(load, i)) {
            continue;
        }
        if (!m_rob.is_older_than(s.rob_idx, load.rob_idx)) {
            continue;
        }
        if (!s.addr_ready || s.addr_is_virtual) {
            continue;
        }
        if (!dword_addr_match(s.addr, addr)) {
            continue;
        }
        const u8 store_mask = gen_byte_mask(s.addr, s.ls_width ? s.ls_width : 8);
        if (!mask_overlap(store_mask, load_mask)) {
            continue;
        }
        if (!s.data_ready || !mask_contains(store_mask, load_mask)) {
            return true;
        }
    }
    return false;
}

bool LSU::is_load_blocked(size_t ldq_idx) const
{
    if (ldq_idx >= m_block_load_mask.size()) {
        return false;
    }
    return m_block_load_mask[ldq_idx]
        || (ldq_idx < m_p1_block_load_mask.size() && m_p1_block_load_mask[ldq_idx]);
}

void LSU::mark_load_blocked(size_t ldq_idx)
{
    if (ldq_idx < m_block_load_mask.size()) {
        m_block_load_mask[ldq_idx] = true;
    }
}

bool LSU::checkViolation(uint64_t addr, ROBID rob_idx, u8 byte_mask) const
{
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid) {
            continue;
        }
        if (!m_rob.is_older_than(s.rob_idx, rob_idx)) {
            continue;
        }
        if (!s.addr_ready || s.addr_is_virtual) {
            continue;
        }
        if (!dword_addr_match(s.addr, addr)) {
            continue;
        }
        const u8 store_mask = gen_byte_mask(s.addr, s.ls_width ? s.ls_width : 8);
        if (!mask_overlap(store_mask, byte_mask)) {
            continue;
        }
        if (!s.data_ready) {
            return true;
        }
    }
    return false;
}

bool LSU::has_unknown_older_store(u64 addr, u8 byte_mask, ROBID rob_idx) const
{
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid) {
            continue;
        }
        if (!m_rob.is_older_than(s.rob_idx, rob_idx)) {
            continue;
        }
        if (!s.addr_ready || s.addr_is_virtual) {
            return true;
        }
        if (!dword_addr_match(s.addr, addr)) {
            continue;
        }
        const u8 store_mask = gen_byte_mask(s.addr, s.ls_width ? s.ls_width : 8);
        if (mask_overlap(store_mask, byte_mask)) {
            return true;
        }
    }
    return false;
}

bool LSU::should_wait_for_store_set(LQEntry& load_entry)
{
    if (!load_entry.uop) {
        return false;
    }
    const uint32_t ssid = m_ssp.getSSID(load_entry.uop->pc);
    const int64_t dep_store = find_dependent_store(ssid);
    if (dep_store < 0) {
        return false;
    }
    auto* st_ptr = find_store_by_id(static_cast<uint64_t>(dep_store));
    if (!st_ptr) {
        return false;
    }
    auto& st = *st_ptr;
    if (!st.valid || st.committed || !m_rob.is_older_than(st.rob_idx, load_entry.rob_idx)) {
        return false;
    }
    const u8 load_mask = gen_byte_mask(load_entry.addr, load_entry.uop->ls_width);
    if (st.addr_ready && !dword_addr_match(st.addr, load_entry.addr)) {
        return false;
    }
    if (st.addr_ready && st.data_ready
        && mask_contains(gen_byte_mask(st.addr, st.ls_width), load_mask)) {
        return false;
    }
    load_entry.waiting_on_store = true;
    load_entry.wait_store_id = static_cast<uint64_t>(dep_store);
    load_entry.state = WAIT_SQ_CHECK;
    return true;
}

void LSU::detect_store_violations(SQEntry& st)
{
    if (!st.valid || !st.addr_ready || st.addr_is_virtual) {
        return;
    }
    const u8 store_mask = gen_byte_mask(st.addr, st.ls_width);
    for (auto& l : lsu_ld_q) {
        if (!l.valid || !l.executed || l.violation_reported || !l.speculative) {
            continue;
        }
        if (!m_rob.is_older_than(st.rob_idx, l.rob_idx)) {
            continue;
        }
        const u8 load_mask = l.uop ? gen_byte_mask(l.addr, l.uop->ls_width) : 0xFFu;
        if (!dword_addr_match(l.addr, st.addr) || !mask_overlap(load_mask, store_mask)) {
            continue;
        }
        l.order_fail = true;
        send_memory_order_flush(l, st);
        return;
    }
}

void LSU::send_memory_order_flush(LQEntry& load_entry, SQEntry& store_entry)
{
    if (!load_entry.uop) {
        return;
    }

    load_entry.violation_reported = true;
    uint32_t ssid = m_ssp.mergeSSID(load_entry.uop->pc,
        store_entry.uop ? store_entry.uop->pc : store_entry.addr);
    m_ssp.updateLFST(ssid, store_entry.store_id);

    FlushEvent event {};
    event.valid = true;
    event.type = FlushType::MEMORY_ORDER;
    event.source = FlushSource::LSU;
    event.scope = FlushScope::FROM_RID;
    event.rid = load_entry.rob_idx;
    event.redirect = true;
    event.redirect_pc = load_entry.uop->pc;
    event.inst_pc = load_entry.uop->pc;
    event.fetch_id = load_entry.uop->fetch_id;
    event.replay_load_idx = load_entry.uop->load_idx;
    event.replay_store_idx = store_entry.store_id;
    event.epoch = load_entry.uop->epoch;
    m_flush_req_q.write(event);
}

bool LSU::can_alloc(MInstPtr inst) const
{
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->type == InstType::LOAD) {
        return !ldq_full();
    }
    if (inst->type == InstType::STA || inst->type == InstType::STORE) {
        return !stq_full();
    }
    if (inst->type == InstType::STD) {
        auto token_it = m_store_token_map.find(inst->store_idx);
        if (token_it != m_store_token_map.end() && token_it->second < lsu_st_q.size()) {
            return lsu_st_q[token_it->second].valid;
        }
        for (const auto& entry : lsu_st_q) {
            if (entry.valid && entry.store_id == inst->store_idx) {
                return true;
            }
        }
        return false;
    }
    return false;
}

bool LSU::can_dispatch(MInstPtr inst) const
{
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->type == InstType::LOAD) {
        for (const auto& entry : lsu_ld_q) {
            if (entry.valid && entry.load_id == inst->load_idx) {
                return true;
            }
        }
        return false;
    }
    if (inst->type == InstType::STA || inst->type == InstType::STD || inst->type == InstType::STORE) {
        for (const auto& entry : lsu_st_q) {
            if (entry.valid && entry.store_id == inst->store_idx) {
                return true;
            }
        }
        return false;
    }
    return false;
}

bool LSU::allocate(MInstPtr inst)
{
    if (!can_alloc(inst)) {
        return false;
    }
    if (inst->type == InstType::LOAD) {
        int idx = alloc_load_entry();
        if (idx < 0) {
            return false;
        }
        auto& entry = lsu_ld_q[static_cast<size_t>(idx)];
        const uint64_t load_id = ++m_ld_id;
        entry = LQEntry {};
        entry.valid = true;
        entry.uop = inst;
        entry.load_id = load_id;
        entry.ldq_idx = static_cast<size_t>(idx);
        entry.rob_idx = inst->rid;
        entry.state = WAIT_ADDR;
        entry.st_dep_mask[0] = m_live_store_mask[0];
        entry.st_dep_mask[1] = m_live_store_mask[1];
        entry.youngest_stq_idx = m_stq_tail;
        inst->load_idx = load_id;
        return true;
    }
    if (inst->type == InstType::STA || inst->type == InstType::STORE) {
        const u64 store_token = inst->store_idx;
        int idx = alloc_store_entry();
        if (idx < 0) {
            return false;
        }
        auto& entry = lsu_st_q[static_cast<size_t>(idx)];
        const uint64_t store_id = ++m_st_id;
        entry = SQEntry {};
        entry.valid = true;
        entry.uop = inst;
        entry.rob_idx = inst->rid;
        entry.store_id = store_id;
        entry.store_token = store_token;
        entry.stq_idx = static_cast<size_t>(idx);
        entry.state = WAIT_ADDR;
        entry.ls_width = inst->ls_width;
        mask_bit_set(m_live_store_mask, static_cast<size_t>(idx));
        inst->ssid = m_ssp.getOrCreateSSID(inst->pc);
        m_ssp.updateLFST(inst->ssid, entry.store_id);
        if (store_token != 0) {
            m_store_token_map[store_token] = static_cast<size_t>(idx);
        }
        inst->store_idx = store_id;
        return true;
    }
    if (inst->type == InstType::STD) {
        SQEntry* sq = find_sq_for_std(inst);
        if (!sq) {
            return false;
        }
        inst->store_idx = sq->store_id;
        if (sq->ls_width == 0) {
            sq->ls_width = inst->ls_width;
        }
        return true;
    }
    return true;
}

SQEntry* LSU::find_sq_for_std(MInstPtr inst)
{
    if (!inst) {
        return nullptr;
    }
    auto token_it = m_store_token_map.find(inst->store_idx);
    if (token_it != m_store_token_map.end() && token_it->second < lsu_st_q.size()) {
        auto& entry = lsu_st_q[token_it->second];
        if (entry.valid) {
            inst->store_idx = entry.store_id;
            return &entry;
        }
    }
    if (auto* by_id = find_store_by_id(inst->store_idx)) {
        return by_id;
    }
    for (auto& s : lsu_st_q) {
        if (!s.valid || !s.uop) {
            continue;
        }
        if (s.uop->pc == inst->pc && s.uop->fetch_id == inst->fetch_id) {
            inst->store_idx = s.store_id;
            return &s;
        }
    }
    return nullptr;
}

SQEntry* LSU::find_store_by_id(uint64_t store_id)
{
    for (auto& s : lsu_st_q) {
        if (s.valid && s.store_id == store_id) {
            return &s;
        }
    }
    return nullptr;
}

LQEntry* LSU::find_load_by_id(uint64_t load_id)
{
    for (auto& l : lsu_ld_q) {
        if (l.valid && l.load_id == load_id) {
            return &l;
        }
    }
    return nullptr;
}

LQEntry* LSU::find_load_by_ldq_idx(size_t idx)
{
    if (idx >= lsu_ld_q.size()) {
        return nullptr;
    }
    auto& l = lsu_ld_q[idx];
    return l.valid ? &l : nullptr;
}

SQEntry* LSU::find_store_by_stq_idx(size_t idx)
{
    if (idx >= lsu_st_q.size()) {
        return nullptr;
    }
    auto& s = lsu_st_q[idx];
    return s.valid ? &s : nullptr;
}

SQEntry* LSU::find_store_by_rid(ROBID rid)
{
    for (auto& s : lsu_st_q) {
        if (s.valid && s.rob_idx.idx == rid.idx && s.rob_idx.direct == rid.direct) {
            return &s;
        }
    }
    return nullptr;
}

LQEntry* LSU::find_load_by_rid(ROBID rid)
{
    for (auto& l : lsu_ld_q) {
        if (l.valid && l.rob_idx.idx == rid.idx && l.rob_idx.direct == rid.direct) {
            return &l;
        }
    }
    return nullptr;
}

void LSU::on_store_operands_updated(SQEntry& st)
{
    if (!st.valid) {
        return;
    }
    if (st.addr_ready && (st.pa == 0 || st.addr_is_virtual)) {
        m_pending_sta_tlb.push_back(st.stq_idx);
    }
    if (st.addr_ready) {
        detect_store_violations(st);
    }
    if (st.addr_ready && st.data_ready) {
        const u8 store_mask = gen_byte_mask(st.addr, st.ls_width ? st.ls_width : 8);
        for (auto& l : lsu_ld_q) {
            if (!l.valid) {
                continue;
            }
            if (l.waiting_on_store && l.wait_store_id == st.store_id) {
                l.waiting_on_store = false;
                l.wait_store_id = static_cast<uint64_t>(-1);
                l.state = WAIT_ADDR;
            }
            if (l.executed || l.in_flight) {
                continue;
            }
            if (!dword_addr_match(l.addr, st.addr)) {
                continue;
            }
            const u8 load_mask = l.uop ? gen_byte_mask(l.addr, l.uop->ls_width) : 0xFFu;
            if (mask_overlap(load_mask, store_mask)) {
                l.state = WAIT_ADDR;
            }
        }
    }
}

bool LSU::store_commit_ready(ROBID rid) const
{
    for (const auto& s : lsu_st_q) {
        if (s.valid && !s.committed && s.rob_idx.idx == rid.idx && s.rob_idx.direct == rid.direct) {
            return s.addr_ready && s.data_ready && !s.addr_is_virtual;
        }
    }
    return true;
}

void LSU::write_memory(u64 addr, u64 data, u32 width)
{
    switch (width) {
    case 1:
        m_mem.store8(addr, static_cast<u8>(data));
        break;
    case 2:
        m_mem.store16(addr, static_cast<u16>(data));
        break;
    case 4:
        m_mem.store32(addr, static_cast<u32>(data));
        break;
    case 8:
    default:
        m_mem.store64(addr, data);
        break;
    }
}

void LSU::update_cache_line(u64 addr)
{
    const u64 line_addr = round_down(addr, m_conf.dcache_line_size);
    m_dcache.invalidate_line(line_addr);

    MemResp resp {};
    resp.valid = true;
    resp.line_addr = line_addr;
    auto line = m_mem.load_cache_line(line_addr, m_conf.dcache_line_size);
    std::memset(resp.data, 0, sizeof(resp.data));
    std::memcpy(resp.data, line.data(), std::min(line.size(), sizeof(resp.data)));
    m_dcache.write_line_async(resp);
}

void LSU::commit_store(ROBID rid)
{
    for (auto& s : lsu_st_q) {
        if (!s.valid || s.committed) {
            continue;
        }
        if (s.rob_idx.idx != rid.idx || s.rob_idx.direct != rid.direct) {
            continue;
        }
        if (!s.addr_ready || !s.data_ready || s.addr_is_virtual) {
            return;
        }
        s.committed = true;
        m_stq_commit_head = wrap_inc(m_stq_commit_head, m_store_q_size);
        if (s.store_token != 0) {
            m_store_token_map.erase(s.store_token);
        }
        if (s.uop) {
            m_ssp.clearLFST(s.uop->ssid, s.store_id);
        }
        return;
    }
}

void LSU::retire_load(ROBID rid)
{
    LQEntry* lq = find_load_by_rid(rid);
    if (!lq) {
        return;
    }
    const size_t idx = lq->ldq_idx;
    *lq = LQEntry {};
    if (idx == m_ldq_head) {
        while (m_ldq_head != m_ldq_tail && !lsu_ld_q[m_ldq_head].valid) {
            m_ldq_head = wrap_inc(m_ldq_head, m_load_q_size);
        }
    }
}

u64 LSU::extract_load_data(const MemResp& resp, u64 addr, u32 width, u32 line_size)
{
    u64 offset = addr % line_size;
    u64 val = 0;
    if (offset + width <= line_size) {
        std::memcpy(&val, resp.data + offset, width);
    }
    return val;
}

bool LSU::commit_ready() const
{
    return !m_complete_q.empty();
}

MInstPtr LSU::commit_inst() const
{
    return m_complete_q.front();
}

void LSU::commit_ack()
{
    MInstPtr inst = nullptr;
    m_complete_q.pop(inst);
}

int LSU::alloc_load_entry()
{
    if (ldq_full()) {
        m_stall = STORE_BUFFER_FULL;
        return -1;
    }
    const int idx = static_cast<int>(m_ldq_tail);
    m_ldq_tail = wrap_inc(m_ldq_tail, m_load_q_size);
    return idx;
}

int LSU::alloc_store_entry()
{
    if (stq_full()) {
        m_stall = STORE_BUFFER_FULL;
        return -1;
    }
    const int idx = static_cast<int>(m_stq_tail);
    m_stq_tail = wrap_inc(m_stq_tail, m_store_q_size);
    return idx;
}

void LSU::replay(LQEntry& load_entry)
{
    load_entry.executed = false;
    load_entry.in_flight = false;
    load_entry.value_ready = false;
    load_entry.succeeded = false;
    load_entry.forward_std_val = false;
    load_entry.speculative = false;
    load_entry.state = WAIT_SQ_CHECK;
    load_entry.waiting_on_store = true;
}

void LSU::replay(SQEntry& store_entry)
{
    store_entry.data_ready = false;
    store_entry.state = WAIT_ADDR;
}

void LSU::flush(const FlushEvent& event)
{
    auto should_flush = [&](const ROBID& rid) {
        switch (event.scope) {
        case FlushScope::ALL:
            return true;
        case FlushScope::YOUNGER_THAN:
            return m_rob.is_older_than(event.rid, rid);
        case FlushScope::FROM_RID:
            return same_rob_id(event.rid, rid) || m_rob.is_older_than(event.rid, rid);
        case FlushScope::NONE:
            return false;
        default:
            return false;
        }
    };
    for (auto& entry : lsu_ld_q) {
        if (entry.valid && should_flush(entry.rob_idx)) {
            if (entry.uop) {
                entry.uop->valid = false;
                entry.uop->state = InstState::FLUSHED;
            }
            entry = LQEntry {};
        }
    }
    for (auto& entry : lsu_st_q) {
        if (entry.valid && should_flush(entry.rob_idx)) {
            if (entry.uop) {
                entry.uop->valid = false;
                entry.uop->state = InstState::FLUSHED;
            }
            if (entry.store_token != 0) {
                m_store_token_map.erase(entry.store_token);
            }
            entry = SQEntry {};
        }
    }

    update_live_store_mask();

    RingQueue<MInstPtr> kept(m_complete_q.capacity());
    MInstPtr inst = nullptr;
    while (m_complete_q.pop(inst)) {
        if (inst && should_flush(inst->rid)) {
            inst->valid = false;
            inst->state = InstState::FLUSHED;
        } else {
            kept.write(inst);
        }
    }
    m_complete_q = std::move(kept);

    m_wb_pending.erase(
        std::remove_if(m_wb_pending.begin(), m_wb_pending.end(),
            [&](const MInstPtr& inst) {
                if (inst && should_flush(inst->rid)) {
                    inst->valid = false;
                    inst->state = InstState::FLUSHED;
                    return true;
                }
                return false;
            }),
        m_wb_pending.end());

    m_pending_mem_resps.erase(
        std::remove_if(m_pending_mem_resps.begin(), m_pending_mem_resps.end(),
            [&](const PendingMemResp& pending) {
                auto* load = find_load_by_id(pending.load_id);
                return !load || should_flush(load->rob_idx);
            }),
        m_pending_mem_resps.end());

    m_wb_pipe.erase(
        std::remove_if(m_wb_pipe.begin(), m_wb_pipe.end(),
            [&](const LSUWBSlot& slot) {
                auto* load = find_load_by_id(slot.load_id);
                return !load || should_flush(load->rob_idx);
            }),
        m_wb_pipe.end());

    m_pending_sta_tlb.erase(
        std::remove_if(m_pending_sta_tlb.begin(), m_pending_sta_tlb.end(),
            [&](size_t stq_idx) {
                auto* store = find_store_by_stq_idx(stq_idx);
                return !store || should_flush(store->rob_idx);
            }),
        m_pending_sta_tlb.end());

    for (auto& s : m_exe_slots) {
        auto* load = find_load_by_ldq_idx(s.ldq_idx);
        if (!load || should_flush(load->rob_idx)) {
            s.valid = false;
        }
    }
    for (auto& s : m_s1_slots) {
        auto* load = find_load_by_ldq_idx(s.ldq_idx);
        if (!load || should_flush(load->rob_idx)) {
            s.valid = false;
        }
    }
}

void LSU::reset()
{
    for (auto& entry : lsu_ld_q) {
        entry = LQEntry {};
    }
    for (auto& entry : lsu_st_q) {
        entry = SQEntry {};
    }
    m_complete_q.clear();
    m_wb_pending.clear();
    m_pending_mem_resps.clear();
    m_wb_pipe.clear();
    m_pending_sta_tlb.clear();
    m_store_token_map.clear();
    m_stall = NO_STALL;
    m_ld_id = 0;
    m_st_id = 0;
    m_ldq_head = m_ldq_tail = 0;
    m_stq_head = m_stq_tail = m_stq_commit_head = m_stq_execute_head = 0;
    m_live_store_mask[0] = m_live_store_mask[1] = 0;
    if (!m_block_load_mask.empty()) {
        std::fill(m_block_load_mask.begin(), m_block_load_mask.end(), false);
        std::fill(m_p1_block_load_mask.begin(), m_p1_block_load_mask.end(), false);
    }
    for (auto& s : m_exe_slots) {
        s.valid = false;
    }
    for (auto& s : m_s1_slots) {
        s.valid = false;
    }
}

int64_t LSU::find_dependent_store(uint32_t ssid)
{
    const int64_t store_id = m_ssp.getLFST(ssid);
    if (store_id < 0) {
        return -1;
    }
    const auto* st = const_cast<LSU*>(this)->find_store_by_id(static_cast<uint64_t>(store_id));
    if (!st || !st->valid || st->committed) {
        return -1;
    }
    return store_id;
}
