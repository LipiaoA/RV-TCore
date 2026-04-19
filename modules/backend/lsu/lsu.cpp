#include "lsu.h"

#include <string>

LSU::LSU(const LSUConfig& conf, const LSUDeps& deps)
    : m_conf(conf)
    , m_dcache(*deps.dcache)
    , m_dtlb(*deps.dtlb)
    , m_rob(*deps.rob)
{
}

void LSU::build() {
    m_issue_width = 2;
    m_load_q_size = 128;
    m_store_q_size = 128;
    m_agus_num = 4;
    m_mshr_buffer_size = 128;
    m_agus.init(m_agus_num, AGUConfig{stages: 8});
    m_mshr_buffer.resize(m_mshr_buffer_size);
    m_load_q.resize(m_load_q_size);
    m_store_q.resize(m_store_q_size);
}

LSU::~LSU() {
    m_load_q.clear();
    m_store_q.clear();
    m_mshr_buffer.clear();
}

void LSU::tick()
{
    m_sched.tick();
    flush();
    schedule();
}

void LSU::dispatch(MicroOp& uop)
{
    for (int i = 0; i < m_agus_num; i++) {
        if (m_agus[i].accept()) {
            m_agus[i].dispatch(&uop);
            break;
        }
    }
}

void LSU::schedule()
{
    int issued = 0;
    for (auto &entry : m_load_q) {
        if (entry.valid && entry.addr_ready) {
            exec_load(entry);
            issued++;
        }
        if (issued >= static_cast<int>(m_issue_width))
            break;
    }
    for (auto &entry : m_store_q) {
        if (issued >= static_cast<int>(m_issue_width))
            break;
        if (entry.valid && entry.addr_ready && entry.data_ready) {
            exec_store(entry);
            issued++;
            entry.state = LdStState::READY;
        }
    }
}

void LSU::exec_store(SQEntry& st)
{
    StData st_data{};
    st_data.valid = true;
    st_data.addr = st.addr;
    st_data.data = st.data;
    st_data.store_id = st.store_id;
    st_data.rob_idx = st.rob_idx;
    m_mshr_buffer.write(st_data);
}

void LSU::exec_load(LQEntry& load_entry)
{
    auto [ok, pa] = m_dtlb.lookup_a(load_entry.addr);
    if (!ok) {
        load_entry.state = LdStState::WAIT_TLB;
        return;
    }
    (void)pa;
    load_entry.state = LdStState::WAIT_CACHE;
}

std::optional<uint64_t> LSU::forward(uint64_t addr, ROBID rob_idx) {
    for (int i = static_cast<int>(m_store_q.size()) - 1; i >= 0; i--) {
        auto& s = m_store_q[static_cast<size_t>(i)];
        if (!s.valid)
            continue;

        if (m_rob.is_older_than(s.rob_idx, rob_idx))
            continue;

        if (s.addr_ready && s.addr == addr && s.data_ready) {
            return s.data;
        }
    }
    return std::nullopt;
}

void LSU::detect_violation(LQEntry& load_entry) {
    for (auto& s : m_store_q) {
        if (!s.valid)
            continue;

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
    for (auto& s : m_store_q) {
        if (!s.valid)
            continue;

        if (m_rob.is_older_than(s.rob_idx, rob_idx)) {
            if (s.addr_ready && s.addr == addr && !s.data_ready) {
                return true;
            }
        }
    }
    return false;
}

void LSU::commit(uint64_t rob_idx) {
    (void)rob_idx;
}

int LSU::alloc_load_entry() {
    for (int i = 0; i < static_cast<int>(m_load_q.size()); i++) {
        if (!m_load_q[static_cast<size_t>(i)].valid)
            return i;
    }
    return -1;
}

int LSU::alloc_store_entry() {
    for (int i = 0; i < static_cast<int>(m_store_q.size()); i++) {
        if (!m_store_q[static_cast<size_t>(i)].valid)
            return i;
    }
    return -1;
}

void LSU::replay(LQEntry& load_entry) {
    load_entry.executed = false;
    load_entry.state = LdStState::WAIT_TLB;
}

void LSU::replay(SQEntry& store_entry) {
    store_entry.valid = false;
    store_entry.state = LdStState::WAIT_ADDR;
}

void LSU::flush() {
}

void LSU::reset() {
}

int LSU::find_dependent_store(uint32_t ssid) {
    (void)ssid;
    return -1;
}
