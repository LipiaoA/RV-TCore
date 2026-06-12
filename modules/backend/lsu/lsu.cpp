#include <string>
#include <utility>
#include <algorithm>
#include <cstring>

#include "lsu.h"

u32 LSU::lsu_idx = 0;

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

void LSU::build() {
    m_complete_q.clear();
    lsu_ld_q.resize(m_load_q_size);
    lsu_st_q.resize(m_store_q_size);
    m_ld_id = 0;
    m_st_id = 0;
}

LSU::~LSU() {
    lsu_ld_q.clear();
    lsu_st_q.clear();
}

void LSU::tick()
{
    drain_writebacks();
    schedule();
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

bool LSU::can_alloc(MInstPtr inst) const
{
    if (!inst || !inst->valid) {
        return false;
    }
    if (inst->type == InstType::LOAD) {
        for (const auto& entry : lsu_ld_q) {
            if (!entry.valid) {
                return true;
            }
        }
        return lsu_ld_q.size() < m_load_q_size;
    }
    if (inst->type == InstType::STA || inst->type == InstType::STORE) {
        for (const auto& entry : lsu_st_q) {
            if (!entry.valid || entry.committed) {
                return true;
            }
        }
        return lsu_st_q.size() < m_store_q_size;
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
        entry.rob_idx = inst->rid;
        entry.state = WAIT_ADDR;
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
        entry.state = WAIT_ADDR;
        entry.ls_width = inst->ls_width;
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

void LSU::schedule()
{
    std::vector<size_t> ready_loads;
    ready_loads.reserve(lsu_ld_q.size());
    for (size_t i = 0; i < lsu_ld_q.size(); ++i) {
        auto& entry = lsu_ld_q[i];
        if (entry.valid && entry.addr_ready && !entry.in_flight && !entry.executed
            && (entry.state == WAIT_ADDR || entry.state == WAIT_SQ_CHECK)) {
            ready_loads.push_back(i);
        }
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
        if (checkViolation(entry.addr, entry.rob_idx)) {
            entry.state = WAIT_SQ_CHECK;
            entry.waiting_on_store = true;
            continue;
        }
        entry.speculative = has_unknown_older_store(entry.addr, entry.rob_idx);
        issue_load(entry);
        ++issued;
    }

    for (auto& s : lsu_st_q) {
        if (s.valid && s.data_ready && s.addr_ready && !s.sta_completed) {
            auto fwd_it = std::find_if(lsu_ld_q.begin(), lsu_ld_q.end(),
                [&](const LQEntry& l) {
                    return l.valid && l.waiting_on_store && l.addr == s.addr
                        && m_rob.is_older_than(s.rob_idx, l.rob_idx);
                });
            if (fwd_it != lsu_ld_q.end()) {
                fwd_it->state = WAIT_ADDR;
                fwd_it->waiting_on_store = false;
            }
        }
    }

    for (auto& entry : lsu_st_q) {
        if (entry.valid && entry.addr_ready && !entry.sta_completed) {
            try_complete_sta(entry);
        }
    }
}

void LSU::issue_load(LQEntry& load_entry)
{
    auto fwd = forward(load_entry.addr, load_entry.rob_idx);
    if (fwd.has_value()) {
        complete_load(load_entry.load_id, fwd.value());
        return;
    }
    start_tlb_lookup(load_entry);
}

void LSU::start_tlb_lookup(LQEntry& load_entry)
{
    load_entry.in_flight = true;
    load_entry.state = WAIT_TLB;
    const uint64_t load_id = load_entry.load_id;
    const u64 va = load_entry.addr;
    m_sched.schedule_after(m_conf.dtlb_hit_latency, [this, load_id, va]() {
        auto* entry_ptr = find_load_by_id(load_id);
        if (!entry_ptr || !entry_ptr->valid || !entry_ptr->in_flight) {
            return;
        }
        auto& entry = *entry_ptr;
        auto [ok, pa] = m_dtlb.lookup_a(va);
        if (!ok) {
            pa = va;
        }
        entry.pa = pa;
        entry.state = WAIT_CACHE;
        start_cache_access(entry, pa);
    });
}

void LSU::start_cache_access(LQEntry& load_entry, u64 pa)
{
    const uint64_t load_id = load_entry.load_id;
    const u64 byte_addr = load_entry.addr;
    const u32 width = load_entry.uop ? load_entry.uop->ls_width : 8;
    const u32 line_size = m_conf.dcache_line_size;

    MemReq req {};
    req.valid = true;
    req.line_addr = round_down(pa, line_size);
    req.epoch = load_entry.uop ? load_entry.uop->epoch : 0;

    m_dcache.fetch_line_async(req).then([this, load_id, byte_addr, width, line_size](const MemResp& resp) {
        auto* entry_ptr = find_load_by_id(load_id);
        if (!entry_ptr || !entry_ptr->valid) {
            return;
        }
        auto& entry = *entry_ptr;
        u64 data = 0;
        if (resp.valid) {
            data = extract_load_data(resp, byte_addr, width, line_size);
        }
        auto fwd = forward(byte_addr, entry.rob_idx);
        if (fwd.has_value()) {
            data = fwd.value();
        }
        complete_load(load_id, data);
    });
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

void LSU::complete_load(uint64_t load_id, uint64_t data)
{
    auto* entry_ptr = find_load_by_id(load_id);
    if (!entry_ptr || !entry_ptr->valid) {
        return;
    }
    auto& entry = *entry_ptr;

    if (checkViolation(entry.addr, entry.rob_idx)) {
        replay(entry);
        if (entry.uop) {
            for (const auto& s : lsu_st_q) {
                if (s.valid && s.uop && m_rob.is_older_than(s.rob_idx, entry.rob_idx)
                    && s.addr_ready && s.addr == entry.addr && !s.data_ready) {
                    m_ssp.mergeSSID(entry.uop->pc, s.uop->pc);
                    break;
                }
            }
        }
        return;
    }

    entry.in_flight = false;
    entry.executed = true;
    entry.value_ready = true;
    entry.data = data;
    entry.state = READY;

    if (entry.uop) {
        entry.uop->rd.val = data;
        entry.uop->load_data = data;
        wakeup_load(entry.uop);
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

void LSU::wakeup_load(MInstPtr inst)
{
    if (!inst || !inst->rd.valid) {
        return;
    }
    WakeupEvent event {};
    event.valid = true;
    event.type = WAKEUP_BYPASS;
    event.ptag = inst->rd.ptag;
    event.rid = inst->rid;
    m_wakeup_bus.push_back(event);
}

std::optional<uint64_t> LSU::forward(uint64_t addr, ROBID rob_idx) {
    for (int i = static_cast<int>(lsu_st_q.size()) - 1; i >= 0; i--) {
        auto& s = lsu_st_q[static_cast<size_t>(i)];
        if (!s.valid) {
            continue;
        }

        if (!m_rob.is_older_than(s.rob_idx, rob_idx)) {
            continue;
        }

        if (s.addr_ready && s.addr == addr && s.data_ready) {
            return s.data;
        }
    }
    return std::nullopt;
}

void LSU::detect_violation(LQEntry& load_entry) {
    for (auto& s : lsu_st_q) {
        if (!s.valid || s.committed) {
            continue;
        }

        if (m_rob.is_older_than(s.rob_idx, load_entry.rob_idx)) {
            if (s.addr_ready && s.addr == load_entry.addr && !s.data_ready) {
                replay(load_entry);
                if (load_entry.uop) {
                    m_ssp.mergeSSID(load_entry.uop->pc, load_entry.addr);
                }
                return;
            }
        }
    }
}

bool LSU::checkViolation(uint64_t addr, ROBID rob_idx) {
    for (auto& s : lsu_st_q) {
        if (!s.valid) {
            continue;
        }

        if (m_rob.is_older_than(s.rob_idx, rob_idx)) {
            if (s.addr_ready && s.addr == addr && !s.data_ready) {
                return true;
            }
        }
    }
    return false;
}

bool LSU::has_unknown_older_store(uint64_t addr, ROBID rob_idx) const
{
    (void)addr;
    for (const auto& s : lsu_st_q) {
        if (!s.valid) {
            continue;
        }
        if (m_rob.is_older_than(s.rob_idx, rob_idx) && !s.addr_ready) {
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
    if (st.addr_ready && st.addr != load_entry.addr) {
        return false;
    }
    if (st.addr_ready && st.data_ready) {
        return false;
    }
    load_entry.waiting_on_store = true;
    load_entry.wait_store_id = static_cast<uint64_t>(dep_store);
    load_entry.state = WAIT_SQ_CHECK;
    return true;
}

void LSU::detect_store_violations(SQEntry& st)
{
    if (!st.valid || !st.addr_ready) {
        return;
    }
    for (auto& l : lsu_ld_q) {
        if (!l.valid || !l.value_ready || l.violation_reported || !l.speculative) {
            continue;
        }
        if (!m_rob.is_older_than(st.rob_idx, l.rob_idx)) {
            continue;
        }
        if (l.addr != st.addr) {
            continue;
        }
        send_memory_order_flush(l, st);
        return;
    }
}

void LSU::send_memory_order_flush(LQEntry& load_entry, SQEntry& store_entry)
{
    (void)store_entry;
    if (!load_entry.uop) {
        return;
    }

    load_entry.violation_reported = true;
    if (load_entry.uop) {
        uint32_t ssid = m_ssp.mergeSSID(load_entry.uop->pc, store_entry.uop ? store_entry.uop->pc : store_entry.addr);
        m_ssp.updateLFST(ssid, store_entry.store_id);
    }

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
    if (st.addr_ready) {
        detect_store_violations(st);
    }
    if (st.addr_ready && st.data_ready) {
        for (auto& l : lsu_ld_q) {
            if (l.valid && l.waiting_on_store && l.wait_store_id == st.store_id) {
                l.waiting_on_store = false;
                l.wait_store_id = static_cast<uint64_t>(-1);
                l.state = WAIT_ADDR;
            }
        }
    }
}

bool LSU::store_commit_ready(ROBID rid) const
{
    for (const auto& s : lsu_st_q) {
        if (s.valid && !s.committed && s.rob_idx.idx == rid.idx && s.rob_idx.direct == rid.direct) {
            return s.addr_ready && s.data_ready;
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
        if (!s.addr_ready || !s.data_ready) {
            return;
        }
        write_memory(s.addr, s.data, s.ls_width ? s.ls_width : 8);
        update_cache_line(s.addr);
        s.committed = true;
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
    *lq = LQEntry {};
}

void LSU::commit() {
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

int LSU::alloc_load_entry() {
    for (int i = 0; i < static_cast<int>(lsu_ld_q.size()); i++) {
        if (!lsu_ld_q[static_cast<size_t>(i)].valid) {
            return i;
        }
    }
    if (lsu_ld_q.size() < m_load_q_size) {
        lsu_ld_q.emplace_back();
        return static_cast<int>(lsu_ld_q.size() - 1);
    }
    return -1;
}

int LSU::alloc_store_entry() {
    for (int i = 0; i < static_cast<int>(lsu_st_q.size()); i++) {
        if (!lsu_st_q[static_cast<size_t>(i)].valid || lsu_st_q[static_cast<size_t>(i)].committed) {
            return i;
        }
    }
    if (lsu_st_q.size() < m_store_q_size) {
        lsu_st_q.emplace_back();
        return static_cast<int>(lsu_st_q.size() - 1);
    }
    return -1;
}

void LSU::replay(LQEntry& load_entry) {
    load_entry.executed = false;
    load_entry.in_flight = false;
    load_entry.value_ready = false;
    load_entry.speculative = false;
    load_entry.state = WAIT_SQ_CHECK;
    load_entry.waiting_on_store = true;
}

void LSU::replay(SQEntry& store_entry) {
    store_entry.data_ready = false;
    store_entry.state = WAIT_ADDR;
}

void LSU::flush(const FlushEvent& event) {
    auto should_flush = [&](const ROBID& rid) {
        return event.scope == FlushScope::ALL || m_rob.is_older_than(event.rid, rid);
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
}

void LSU::reset() {
    for (auto& entry : lsu_ld_q) {
        entry = LQEntry {};
    }
    for (auto& entry : lsu_st_q) {
        entry = SQEntry {};
    }
    m_complete_q.clear();
    m_wb_pending.clear();
    m_store_token_map.clear();
    m_stall = NO_STALL;
    m_ld_id = 0;
    m_st_id = 0;
}

int64_t LSU::find_dependent_store(uint32_t ssid) {
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
