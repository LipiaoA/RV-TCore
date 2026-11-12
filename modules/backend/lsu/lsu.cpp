#include <string>
#include <utility>
#include <algorithm>
#include <cstring>
#include <tuple>
#include <iostream>

#include "lsu.h"
#include "log.h"

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

const char* ldst_state_str(LdStState state)
{
    switch (state) {
    case WAIT_ADDR:
        return "WAIT_ADDR";
    case WAIT_SQ_CHECK:
        return "WAIT_SQ_CHECK";
    case WAIT_TLB:
        return "WAIT_TLB";
    case WAIT_CACHE:
        return "WAIT_CACHE";
    case WAIT_FILL:
        return "WAIT_FILL";
    case READY:
        return "READY";
    default:
        return "UNKNOWN";
    }
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
      m_store_q_size(conf.store_q_size),
      m_stats(*deps.perf_stats)
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

size_t LSU::wrap_dec(size_t idx, size_t cap)
{
    return (idx - 1 + cap) % cap;
}

bool LSU::st_dep_hit(const LQEntry& e, size_t stq_idx)
{
    return mask_bit_set(e.st_dep_mask, stq_idx);
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

u64 LSU::apply_load_gen(const SQEntry& s_entry, u64 load_addr, u32 load_width)
{

    u64 reg_data = s_entry.data;
    u64 store_addr = s_entry.addr;
    u32 store_width = s_entry.ls_width;

    const u32 store_off = static_cast<u32>(store_addr & 7ULL);
    const u32 load_off  = static_cast<u32>(load_addr & 7ULL);

    u64 dword = 0;
    for (u32 i = 0; i < store_width; ++i) {
        dword |= ((reg_data >> (8 * i)) & 0xFFULL) << (8 * (store_off + i));
    }

    u64 val = dword >> (load_off * 8);

    switch (load_width) {
    case 1:  return val & 0xFFu;
    case 2:  return val & 0xFFFFu;
    case 4:  return val & 0xFFFFFFFFu;
    default: return val;
    }
    return val;
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
    size_t tail = wrap_dec(m_stq_tail, m_store_q_size);
    while (!lsu_st_q[tail].valid) {
        m_stq_tail = tail;
        if (tail == m_stq_head) {
            break;
        }
        tail = wrap_dec(tail, m_store_q_size);
    }
    while (m_stq_head != m_stq_tail && !lsu_st_q[m_stq_head].valid) {
        m_stq_head = wrap_inc(m_stq_head, m_store_q_size);
    }

    tail = wrap_dec(m_ldq_tail, m_load_q_size);
    while (!lsu_ld_q[tail].valid) {
        m_ldq_tail = tail;
        if (tail == m_ldq_head) {
            break;
        }
        tail = wrap_dec(tail, m_load_q_size);
    }

    while (m_ldq_head != m_ldq_tail && !lsu_ld_q[m_ldq_head].valid) {
        m_ldq_head = wrap_inc(m_ldq_head, m_load_q_size);
    }
}

void LSU::tick()
{
    // 逆序流水线: W1 -> drain WB -> STA complete -> S1 -> W0 -> S0(store TLB) -> EXE_S0 -> P0
    m_stall = NO_STALL;
    m_mem_port_busy = false;
    m_tlb_port_busy = false;

    wb();
    for (auto& s : lsu_st_q) {
        if (s.valid && s.addr_ready && !s.addr_is_virtual && !s.sta_completed && s.data_ready) {
            sta(s);
        }
    }
    lc();
    rsp();
    stlb();
    ldx();
    iss();

    if (!m_block_load_mask.empty()) {
        m_p1_block_load_mask = m_block_load_mask;
        std::fill(m_block_load_mask.begin(), m_block_load_mask.end(), false);
    }
    dump_state();
}

void LSU::commit()
{
}

void LSU::wb()
{
    drain_stores();
    writeback_stores();
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

void LSU::rsp()
{
    while (!m_pending_store_resps.empty()) {
        PendingStoreResp pending = m_pending_store_resps.front();
        m_pending_store_resps.pop_front();
        if (pending.stq_idx >= m_store_q_size) {
            continue;
        }
        auto& s = lsu_st_q[pending.stq_idx];
        if (!s.valid || !s.store_in_flight) {
            continue;
        }
        s.store_in_flight = false;
        s.succeeded = true;
        m_stq_execute_head = wrap_inc(m_stq_execute_head, m_store_q_size);
        LOG_LEVEL1("LSU D$ Resp", "complete store pc:0x%lx id:%lu stq:%zu addr:0x%lx rid:%s",
                   s.uop->pc, s.store_id, pending.stq_idx, s.pa ? s.pa : s.addr, s.uop->rid.str());
    }

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
            data = extract_load_data(pending.resp, pending.addr, pending.width,
                                     m_conf.dcache_line_size);
        }
        const u8 load_mask = entry.uop ? gen_byte_mask(pending.addr, entry.uop->ls_width) : 0xFFu;
        auto fwd = try_forward(pending.addr, load_mask, entry, true);
        if (fwd.has_value()) {
            data = fwd->second;
            LOG_LEVEL1("LoadStoreUnit Rsp", "Late forward load_id:%lu stq:%zu addr:0x%lx data:0x%lx",
                       pending.load_id, fwd->first, pending.addr, data);
        }
        LOG_LEVEL1("LoadStoreUnit Rsp", "Resp pc:0x%lx id:%lu req_gen:%lu addr:0x%lx width:%u valid:%d data:0x%lx",
                   entry.uop->pc, pending.load_id, pending.req_gen, pending.addr, pending.width,
                   pending.resp.valid, data);
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

void LSU::lc()
{
    for (auto& slot : m_s1_slots) {
        if (!slot.valid) {
            continue;
        }
        lcam(slot);
        slot.valid = false;
    }
}

void LSU::ldx()
{
    // process execute/stlb stage: TLB lookup and DCache request issue
    for (auto& slot : m_exe_slots) {
        if (!slot.valid) {
            continue;
        }
        do_ldx(slot);
        slot.valid = false;
    }
}

void LSU::do_ldx(LSUExeSlot& slot)
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
    // current implementation is must success.
    const std::pair<bool, u64> tlb_res = m_dtlb.lookup_a(slot.vaddr);
    const bool ok = tlb_res.first;
    const u64 pa = tlb_res.second;
    if (!ok) {
        slot.tlb_miss = true;
        entry->addr_is_virtual = true;
        entry->state = WAIT_TLB;
        LOG_LEVEL1("LSU LDX", "TLB miss pc:0x%lx id:%lu vaddr:0x%lx",
                   entry->uop->pc, entry->load_id, slot.vaddr);
        return;
    }
    slot.paddr = pa;
    entry->pa = pa;
    entry->addr_is_virtual = false;
    LOG_LEVEL1("LSU LDX", "load tlb hit pc:0x%lx id:%lu vaddr:0x%lx paddr:0x%lx mask:0x%x",
               entry->uop->pc, entry->load_id, slot.vaddr, pa, slot.byte_mask);

    if (!grab_mem_port()) {
        m_stall = DCACHE_PORT_FULL;
        entry->state = WAIT_CACHE;
        return;
    }

    issue_mem(*entry, pa, slot.byte_mask);
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
            LOG_LEVEL1("LSU LDX", "execute load pc:0x%lx id:%lu ldq:%zu vaddr:0x%lx paddr:0x%lx mask:0x%x mem_issued:%d",
                       entry->uop->pc, entry->load_id, slot.ldq_idx, slot.vaddr, pa, slot.byte_mask, slot.mem_issued);
            return;
        }
    }
    m_stall = DCACHE_PORT_FULL;
}

void LSU::lcam(LSUS1Slot& slot)
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
            // TODO: Add speculative load wakeup
            // wakeup_load(entry->uop, true);
        }
        LOG_LEVEL1("LSU LCAM", "forward pc:0x%lx load_id:%lu ldq:%zu stq:%zu addr:0x%lx data:0x%lx",
                   entry->uop->pc, entry->load_id, slot.ldq_idx, fwd->first, slot.vaddr, fwd->second);
        return;
    }

    if (checkViolation(slot.vaddr, entry->rob_idx, load_mask)
        || sq_conflict(slot.vaddr, load_mask, *entry)) {
        slot.killed = true;
        recover_load_misspec(*entry, nullptr);
        LOG_LEVEL1("LSU LCAM", "replay load_id:%lu ldq:%zu addr:0x%lx mask:0x%x",
                   entry->load_id, slot.ldq_idx, slot.vaddr, load_mask);
        return;
    }

    if (slot.mem_issued) {
        entry->executed = true;
        LOG_LEVEL1("LSU LCAM", "load pass load_id:%lu ldq:%zu addr:0x%lx mem_issued:%d",
                   entry->load_id, slot.ldq_idx, slot.vaddr, slot.mem_issued);
    }
}

void LSU::issue_mem(LQEntry& entry, u64 pa, u8 byte_mask)
{
    (void)byte_mask;
    ++entry.mem_req_gen;
    entry.in_flight = true;
    entry.state = WAIT_CACHE;
    entry.speculative = is_spec_load(entry.addr,
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

    LOG_LEVEL1("LSU", "DCache Req load_id:%lu req_gen:%lu paddr:0x%lx line:0x%lx vaddr:0x%lx width:%u speculative:%d",
               load_id, req_gen, pa, req.line_addr, byte_addr, width, entry.speculative);
    m_dcache.fetch_line_async(req).then([this, load_id, req_gen, byte_addr, width](const MemResp& resp) {
        PendingMemResp pending {};
        pending.load_id = load_id;
        pending.req_gen = req_gen;
        pending.addr = byte_addr;
        pending.width = width;
        pending.resp = resp;
        m_pending_mem_resps.push_back(pending);
        LOG_LEVEL1("LSU", "DCache Resp load_id:%lu req_gen:%lu addr:0x%lx width:%u valid:%d",
                   load_id, req_gen, byte_addr, width, resp.valid);
        u64 dat[8];
        memcpy(dat, resp.data, 8 * sizeof(u64));
        LOG_LEVEL1("LSU", "DCache Resp Line Addr:0x%lx, Data:0x%lx 0x%lx 0x%lx "
            "0x%lx 0x%lx 0x%lx 0x%lx 0x%lx", resp.line_addr, dat[0], dat[1],
            dat[2], dat[3], dat[4], dat[5], dat[6], dat[7]);
    });
}

void LSU::stlb()
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
    const std::pair<bool, u64> tlb_res = m_dtlb.lookup_a(st->addr);
    const bool ok = tlb_res.first;
    const u64 pa = tlb_res.second;

    if (!ok) {
        st->addr_is_virtual = true;
        m_pending_sta_tlb.push_back(stq_idx);
        LOG_LEVEL1("LSU STA", "TLB miss stq:%zu store_id:%lu vaddr:0x%lx",
                   stq_idx, st->store_id, st->addr);
        return;
    }
    st->pa = pa;
    st->addr_is_virtual = false;
    LOG_LEVEL1("LSU STA", "TLB hit pc:0x%lx id:%lu st_idx:%zu vaddr:0x%lx paddr:0x%lx, rid:%s",
               st->uop->pc, st->store_id, stq_idx, st->addr, pa, st->uop->rid.str());
    //FIXME: Flush Type should be LOAD_REPLAY
    check_mem_order(*st);
}

void LSU::iss()
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
        if (load_blocked(i)) {
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
        if (store_set_block(entry)) {
            LOG_LEVEL1("LSU ISS", "Store-set wait pc:0x%lx load_id:%lu wait_store:%lu addr:0x%lx",
                       entry.uop->pc, entry.load_id, entry.wait_store_id, entry.addr);
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
            || sq_conflict(entry.addr, load_mask, entry)) {
            entry.state = WAIT_SQ_CHECK;
            entry.waiting_on_store = true;
            LOG_LEVEL1("LSU ISS", "Store Conflict pc:0x%lx load_id:%lu addr:0x%lx mask:0x%x, rid:%s",
                       entry.uop->pc, entry.load_id, entry.addr, load_mask, entry.uop->rid.str());
            continue;
        }

        fire_load(idx);
        block_load(idx);
        ++issued;
    }
}

void LSU::fire_load(size_t ldq_idx)
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
                // TODO: Add speculative load wakeup
                // wakeup_load(entry.uop, true);
            }
            LOG_LEVEL1("LSU ISS", "Issue load pc:0x%lx id:%lu addr:0x%lx mask:0x%x, waiting store:%d",
                       entry.uop->pc, entry.load_id, entry.addr, load_mask, entry.waiting_on_store);
            return;
        }
    }
    m_stall = TLB_PORT_FULL;
}

bool LSU::grab_mem_port()
{
    if (m_mem_port_busy) {
        return false;
    }
    m_mem_port_busy = true;
    return true;
}

void LSU::drain_stores()
{
    while (lsu_st_q[m_stq_head].valid && lsu_st_q[m_stq_head].committed
           && lsu_st_q[m_stq_head].succeeded) {
        mask_bit_clear(m_live_store_mask, m_stq_head);
        for (size_t i = 0; i < m_load_q_size; ++i) {
            mask_bit_clear(lsu_ld_q[i].st_dep_mask, m_stq_head);
        }
        lsu_st_q[m_stq_head] = SQEntry {};
        m_stq_head = wrap_inc(m_stq_head, m_store_q_size);
    }
}

void LSU::writeback_stores()
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
        if (s.store_in_flight) {
            return;
        }
        if (!s.addr_ready || !s.data_ready || s.addr_is_virtual) {
            return;
        }
        if (!grab_mem_port()) {
            m_stall = DCACHE_PORT_FULL;
            return;
        }
        issue_store(s, idx);
        m_mem_port_busy = false;
        return;
    }
}

void LSU::issue_store(SQEntry& s, size_t stq_idx)
{
    StoreReq req {};
    req.valid = true;
    req.addr = s.pa ? s.pa : s.addr;
    req.data = s.data;
    req.width = s.ls_width ? s.ls_width : 8;
    req.epoch = s.uop ? s.uop->epoch : 0;
    s.store_in_flight = true;
    LOG_LEVEL1("LSU WB", "Issue store pc:0x%lx id:%lu stq:%zu addr:0x%lx data:0x%lx width:%u",
               s.uop->pc, s.store_id, stq_idx, s.addr, s.data, s.ls_width);
    m_dcache.store_merge_async(req).then([this, stq_idx](bool) {
        PendingStoreResp pending {};
        pending.stq_idx = stq_idx;
        m_pending_store_resps.push_back(pending);
    });
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
        recover_load_misspec(entry, nullptr);
        LOG_LEVEL1("LSU WB", "Complete replay pc:0x%lx id:%lu addr:0x%lx mask:0x%x",
                   entry.uop->pc, entry.load_id, entry.addr, load_mask);
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
        m_complete_q.write(entry.uop);
    }
    LOG_LEVEL1("LSU WB", "Complete load pc:0x%lx id:%lu ldq:%zu addr:0x%lx data:0x%lx, rid:%s",
               entry.uop->pc, entry.load_id, entry.ldq_idx, entry.addr, data, entry.uop->rid.str());
}

void LSU::sta(SQEntry& st)
{
    if (!st.uop || st.sta_completed) {
        return;
    }
    // 双半边都齐才完成整条 STORE
    if (!st.addr_ready || !st.data_ready || st.addr_is_virtual) {
        return;
    }
    st.state = READY;
    st.sta_completed = true;
    st.uop->sta_done = true;
    st.uop->std_done = true;
    m_complete_q.write(st.uop);
    LOG_LEVEL1("LSU STA", "Complete store_id:%lu stq:%zu addr:0x%lx paddr:0x%lx, rid:%s",
               st.store_id, st.stq_idx, st.addr, st.pa, st.uop->rid.str());
}

void LSU::wakeup_load(MInstPtr inst, bool speculative)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
    WakeupEvent event {};
    event.valid = true;
    event.type = speculative ? WAKEUP_BYPASS : WAKEUP_WRITEBACK;
    event.ptag = inst->rd.ptag;
    event.rid = inst->rid;
    m_wakeup_bus.push_back(event);
    LOG_LEVEL1("LSU", "Wakeup ptag:p%d rid:%u-%u speculative:%d",
               event.ptag, event.rid.direct, event.rid.idx, speculative);
}

std::optional<std::pair<size_t, u64>> LSU::try_forward(u64 addr, u8 load_mask,
                                                      const LQEntry& load, bool can_forward) const
{
    if (!can_forward) {
        return std::nullopt;
    }
    const size_t fwd_idx = pick_fwd_stq(addr, load_mask, load);
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
    const u64 data = apply_load_gen(s, addr, load.uop->ls_width);

    return std::make_pair(fwd_idx, data);
}

size_t LSU::pick_fwd_stq(u64 addr, u8 load_mask, const LQEntry& load) const
{
    size_t found = m_store_q_size;
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid || !st_dep_hit(load, i)) {
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

bool LSU::sq_conflict(u64 addr, u8 load_mask, const LQEntry& load) const
{
    for (size_t i = 0; i < m_store_q_size; ++i) {
        const auto& s = lsu_st_q[i];
        if (!s.valid || !st_dep_hit(load, i)) {
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

bool LSU::load_blocked(size_t ldq_idx) const
{
    if (ldq_idx >= m_block_load_mask.size()) {
        return false;
    }
    return m_block_load_mask[ldq_idx]
        || (ldq_idx < m_p1_block_load_mask.size() && m_p1_block_load_mask[ldq_idx]);
}

void LSU::block_load(size_t ldq_idx)
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

bool LSU::is_spec_load(u64 addr, u8 byte_mask, ROBID rob_idx) const
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

bool LSU::store_set_block(LQEntry& load_entry)
{
    if (!load_entry.uop) {
        return false;
    }
    const uint32_t ssid = m_ssp.getSSID(load_entry.uop->pc);
    const int64_t dep_store = find_dep_store(ssid);
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

void LSU::check_mem_order(SQEntry& st)
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
        flush_order(l, st);
        return;
    }
}

void LSU::flush_order(LQEntry& load_entry, SQEntry& store_entry)
{
    if (!load_entry.uop) {
        return;
    }
    // 4A: 默认轻量恢复，不再投递 MEMORY_ORDER flush / redirect
    load_entry.violation_reported = true;
    recover_load_misspec(load_entry, &store_entry);
}

void LSU::emit_load_dep_kill(MInstPtr load_uop)
{
    if (!load_uop || !load_uop->rd.valid) {
        return;
    }
    WakeupEvent event {};
    event.valid = true;
    event.type = WAKEUP_REPLAY;
    event.load_dep_kill = true;
    event.ptag = load_uop->rd.ptag;
    event.rid = load_uop->rid;
    event.rtyp = RegType::GPR;
    m_wakeup_bus.push_back(event);
    LOG_LEVEL1("LSU LoadDepKill", "ptag:p%d rid:%u-%u pc:0x%lx",
               event.ptag, event.rid.direct, event.rid.idx, load_uop->pc);
}

void LSU::recover_load_misspec(LQEntry& load_entry, SQEntry* store_entry)
{
    if (store_entry && store_entry->uop && load_entry.uop) {
        uint32_t ssid = m_ssp.mergeSSID(load_entry.uop->pc, store_entry->uop->pc);
        m_ssp.updateLFST(ssid, store_entry->store_id);
    } else if (store_entry && load_entry.uop) {
        uint32_t ssid = m_ssp.mergeSSID(load_entry.uop->pc, store_entry->addr);
        m_ssp.updateLFST(ssid, store_entry->store_id);
    }
    replay(load_entry);
    emit_load_dep_kill(load_entry.uop);
    LOG_LEVEL1("LSU Recover", "1A misspec load_id:%lu no-flush",
               load_entry.load_id);
}

bool LSU::can_alloc(MInstPtr inst) const
{
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->type == InstType::LOAD) {
        return !ldq_full();
    }
    if (inst->type == InstType::STORE || inst->type == InstType::STA) {
        return !stq_full();
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
    if (inst->type == InstType::STORE || inst->type == InstType::STA ||
        inst->type == InstType::STD) {
        for (const auto& entry : lsu_st_q) {
            if (!entry.valid) {
                continue;
            }
            if (entry.rob_idx.idx == inst->rid.idx && entry.rob_idx.direct == inst->rid.direct) {
                return true;
            }
            if (entry.store_id == inst->store_id && inst->store_id != 0) {
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
        LOG_LEVEL1("LSU Alloc", "load ldq:%zu load_id:%lu pc:0x%lx rid:%u-%u dep_mask:%lx:%lx",
                   entry.ldq_idx, entry.load_id, inst->pc, inst->rid.direct, inst->rid.idx,
                   entry.st_dep_mask[1], entry.st_dep_mask[0]);
        return true;
    }
    if (inst->type == InstType::STA || inst->type == InstType::STORE) {
        const u64 store_token = inst->store_id;
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
        inst->store_id = store_id;
        LOG_LEVEL1("LSU Alloc", "store stq:%zu store_id:%lu token:%lu pc:0x%lx rid:%u-%u width:%u",
                   entry.stq_idx, entry.store_id, entry.store_token, inst->pc,
                   inst->rid.direct, inst->rid.idx, entry.ls_width);
        return true;
    }
    return true;
}

SQEntry* LSU::find_sq_for_std(MInstPtr inst)
{
    if (!inst) {
        return nullptr;
    }
    auto token_it = m_store_token_map.find(inst->store_id);
    if (token_it != m_store_token_map.end() && token_it->second < lsu_st_q.size()) {
        auto& entry = lsu_st_q[token_it->second];
        if (entry.valid) {
            inst->store_id = entry.store_id;
            return &entry;
        }
    }
    if (auto* by_id = find_store_by_id(inst->store_id)) {
        return by_id;
    }
    for (auto& s : lsu_st_q) {
        if (!s.valid || !s.uop) {
            continue;
        }
        if (s.uop->pc == inst->pc && s.uop->fetch_id == inst->fetch_id) {
            inst->store_id = s.store_id;
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
        check_mem_order(st);
    }
    if (st.addr_ready && st.data_ready) {
        LOG_LEVEL1("LSU", "operands ready store_id:%lu stq:%zu addr:0x%lx data:0x%lx width:%u, rid:%s",
                   st.store_id, st.stq_idx, st.addr, st.data, st.ls_width,
                   st.rob_idx.str());
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
        LOG_LEVEL1("LSU Commit", "store committed store_id:%lu stq:%zu rid:%s, addr:0x%lx, data:0x%lx, width:%u",
                   s.store_id, s.stq_idx, rid.str(), s.addr, s.data, s.ls_width);
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
    LOG_LEVEL1("LSU Commit", "load retired load_id:%lu ldq:%zu rid:%s, addr:0x%lx, width:%u",
               lq->load_id, idx, rid.str(), lq->addr, lq->uop->ls_width);
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
    if (inst) {
        LOG_LEVEL1("LSU Commit", "ack pc:0x%lx type:%s rid:%u-%u",
                   inst->pc, inst_type_str(inst->type), inst->rid.direct, inst->rid.idx);
    }
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
    LOG_LEVEL1("LSU", "Replay load_id:%lu ldq:%zu addr:0x%lx state:%s",
               load_entry.load_id, load_entry.ldq_idx, load_entry.addr,
               ldst_state_str(load_entry.state));
    load_entry.executed = false;
    load_entry.in_flight = false;
    load_entry.value_ready = false;
    load_entry.succeeded = false;
    load_entry.forward_std_val = false;
    load_entry.speculative = false;
    load_entry.order_fail = false;
    load_entry.state = WAIT_SQ_CHECK;
    load_entry.waiting_on_store = true;
    if (load_entry.uop) {
        load_entry.uop->state = InstState::REPLAY;
        load_entry.uop->executed = false;
    }
}

void LSU::replay(SQEntry& store_entry)
{
    LOG_LEVEL1("LSU", "Replay store_id:%lu stq:%zu addr:0x%lx state:%s",
               store_entry.store_id, store_entry.stq_idx, store_entry.addr,
               ldst_state_str(store_entry.state));
    store_entry.data_ready = false;
    store_entry.state = WAIT_ADDR;
    store_entry.sta_completed = false;
}

void LSU::replay_store_half(ROBID rid, bool kill_sta, bool kill_std)
{
    SQEntry* st = find_store_by_rid(rid);
    if (!st || !st->valid) {
        return;
    }
    if (kill_sta) {
        st->addr_ready = false;
        st->addr_is_virtual = true;
        st->pa = 0;
        st->sta_completed = false;
        st->state = WAIT_ADDR;
        // if (st->uop) {
        //     st->uop->sta_iss = false;
        //     st->uop->sta_done = false;
        //     st->uop->addr_ready = false;
        // }
    }
    if (kill_std) {
        st->data_ready = false;
        st->sta_completed = false;
        // if (st->uop) {
        //     st->uop->std_iss = false;
        //     st->uop->std_done = false;
        //     st->uop->data_ready = false;
        // }
    }
    LOG_LEVEL1("LoadStoreUnit", "Replay store half rid:%s kill_sta:%d kill_std:%d",
               rid.str(), kill_sta, kill_std);
}

void LSU::flush(const FlushEvent& event)
{
    if (!event.valid) {
        return;
    }
    size_t flushed_loads = 0;
    size_t flushed_stores = 0;
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
            ++flushed_loads;
            if (entry.uop) {
                entry.uop->valid = false;
                entry.uop->state = InstState::FLUSHED;
            }
            entry = LQEntry {};
        }
    }
    for (auto& entry : lsu_st_q) {
        if (entry.valid && should_flush(entry.rob_idx)) {
            ++flushed_stores;
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

    m_pending_mem_resps.erase(
        std::remove_if(m_pending_mem_resps.begin(), m_pending_mem_resps.end(),
            [&](const PendingMemResp& pending) {
                auto* load = find_load_by_id(pending.load_id);
                return !load || should_flush(load->rob_idx);
            }),
        m_pending_mem_resps.end());

    m_pending_store_resps.erase(
        std::remove_if(m_pending_store_resps.begin(), m_pending_store_resps.end(),
            [&](const PendingStoreResp& pending) {
                if (pending.stq_idx >= m_store_q_size) {
                    return true;
                }
                return should_flush(lsu_st_q[pending.stq_idx].rob_idx);
            }),
        m_pending_store_resps.end());

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
    LOG_LEVEL1("LoadStoreUnit", "Flush type:%s source:%s scope:%s rid:%u-%u loads:%zu stores:%zu",
               FlushTypeStr[static_cast<int>(event.type)],
               FlushSourceStr[static_cast<int>(event.source)],
               FlushScopeStr[static_cast<int>(event.scope)],
               event.rid.direct, event.rid.idx, flushed_loads, flushed_stores);
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
    m_pending_mem_resps.clear();
    m_pending_store_resps.clear();
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

void LSU::dump_state() const
{
    size_t valid_loads = 0;
    size_t valid_stores = 0;
    size_t inflight_loads = 0;
    size_t ready_stores = 0;

    for (const auto& l : lsu_ld_q) {
        if (!l.valid) {
            continue;
        }
        ++valid_loads;
        if (l.in_flight) {
            ++inflight_loads;
        }
    }
    for (const auto& s : lsu_st_q) {
        if (!s.valid) {
            continue;
        }
        ++valid_stores;
        if (s.addr_ready && s.data_ready && !s.addr_is_virtual) {
            ++ready_stores;
        }
    }

    LOG_LEVEL1("LoadStoreUnit", "ldq:%zu/%zu head:%zu tail:%zu stq:%zu/%zu head:%zu tail:%zu commit:%zu exe:%zu "
               "inflight_ld:%zu ready_st:%zu complete:%zu wb:%zu pending_resp:%zu pending_sta:%zu stall:%d",
               valid_loads, m_load_q_size, m_ldq_head, m_ldq_tail,
               valid_stores, m_store_q_size, m_stq_head, m_stq_tail,
               m_stq_commit_head, m_stq_execute_head, inflight_loads, ready_stores,
               m_complete_q.size(), m_wb_pipe.size(), m_pending_mem_resps.size(),
               m_pending_sta_tlb.size(), static_cast<int>(m_stall));

    for (const auto& s : m_exe_slots) {
        if (!s.valid) {
            continue;
        }
    }
    for (const auto& s : m_s1_slots) {
        if (!s.valid) {
            continue;
        }
    }
}

int64_t LSU::find_dep_store(uint32_t ssid)
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
