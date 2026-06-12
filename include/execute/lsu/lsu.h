#ifndef RV_TCORE_LSU_H
#define RV_TCORE_LSU_H

#include <vector>
#include <optional>
#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "rv_def.h"
#include "module_comm.h"
#include "memory/cache.h"
#include "memory/tlb.h"
#include "memory/mem.h"
#include "rob/rob.h"
#include "util.h"
#include "config.h"

static const int SSIT_SIZE = 1024;
static const int LFST_SIZE = 1024;

struct StoreSetPredictor {
    uint32_t SSIT[SSIT_SIZE];
    int64_t  LFST[LFST_SIZE];
    uint32_t next_id { 1 };

    StoreSetPredictor() {
        memset(SSIT, 0, sizeof(SSIT));
        for (auto& x : LFST) x = -1;
    }

    uint32_t getSSID(uint64_t pc) const
    {
        return SSIT[pc % SSIT_SIZE];
    }

    uint32_t getOrCreateSSID(uint64_t pc)
    {
        uint32_t& ssid = SSIT[pc % SSIT_SIZE];
        if (ssid == 0) {
            ssid = next_id++;
            if (next_id >= LFST_SIZE) {
                next_id = 1;
            }
        }
        return ssid;
    }

    uint32_t mergeSSID(uint64_t load_pc, uint64_t store_pc)
    {
        uint32_t s1 = SSIT[load_pc % SSIT_SIZE];
        uint32_t s2 = SSIT[store_pc % SSIT_SIZE];
        uint32_t new_id = 0;
        if (s1 == 0 && s2 == 0) {
            new_id = next_id++;
            if (next_id >= LFST_SIZE) {
                next_id = 1;
            }
        } else if (s1 == 0) {
            new_id = s2;
        } else if (s2 == 0) {
            new_id = s1;
        } else {
            new_id = std::min(s1, s2);
            const uint32_t old_id = std::max(s1, s2);
            if (old_id < LFST_SIZE && new_id < LFST_SIZE && LFST[new_id] < 0) {
                LFST[new_id] = LFST[old_id];
            }
        }
        SSIT[load_pc % SSIT_SIZE] = new_id;
        SSIT[store_pc % SSIT_SIZE] = new_id;
        return new_id;
    }

    void updateLFST(uint32_t ssid, uint64_t store_id)
    {
        if (ssid > 0 && ssid < LFST_SIZE) {
            LFST[ssid] = static_cast<int64_t>(store_id);
        }
    }

    int64_t getLFST(uint32_t ssid) const
    {
        if (ssid == 0 || ssid >= LFST_SIZE) {
            return -1;
        }
        return LFST[ssid];
    }

    void clearLFST(uint32_t ssid, uint64_t store_id)
    {
        if (ssid > 0 && ssid < LFST_SIZE && LFST[ssid] == static_cast<int64_t>(store_id)) {
            LFST[ssid] = -1;
        }
    }
};

enum LSUStallType {
    NO_STALL = 0,
    TLB_PORT_FULL = 1,
    PTW_FULL = 2,
    DCACHE_PORT_FULL = 3,
    MSHR_FULL = 4,
    STORE_BUFFER_FULL = 5
};

class LSU {
public:
    LSU(const LSUConfig& conf, const LSUDeps& deps);
    ~LSU();
    void                                build();

    bool                                can_dispatch(MInstPtr inst) const;
    bool                                can_alloc(MInstPtr inst) const;
    bool                                can_allocate(MInstPtr inst) const { return can_alloc(inst); }
    bool                                allocate(MInstPtr inst);
    std::vector<LQEntry>&               load_q() { return lsu_ld_q; }
    std::vector<SQEntry>&               store_q() { return lsu_st_q; }
    void                                tick();
    void                                commit();
    void                                flush(const FlushEvent& event);
    void                                reset();

    bool                                commit_ready() const;
    MInstPtr                            commit_inst() const;
    void                                commit_ack();

    bool                                store_commit_ready(ROBID rid) const;
    void                                commit_store(ROBID rid);
    void                                retire_load(ROBID rid);

    int                                 alloc_load_entry();
    int                                 alloc_store_entry();
    void                                replay(LQEntry& load_entry);
    void                                replay(SQEntry& store_entry);
    bool                                checkViolation(uint64_t addr, ROBID rob_idx);

    SQEntry*                            find_sq_for_std(MInstPtr inst);
    SQEntry*                            find_store_by_id(uint64_t store_id);
    LQEntry*                            find_load_by_id(uint64_t load_id);
    void                                on_store_operands_updated(SQEntry& st);

private:
    void                                schedule();
    void                                drain_writebacks();
    void                                issue_load(LQEntry& load_entry);
    void                                start_tlb_lookup(LQEntry& load_entry);
    void                                start_cache_access(LQEntry& load_entry, u64 pa);
    void                                complete_load(uint64_t load_id, uint64_t data);
    void                                try_complete_sta(SQEntry& st);
    SQEntry*                            find_store_by_rid(ROBID rid);
    LQEntry*                            find_load_by_rid(ROBID rid);

    std::optional<uint64_t>             forward(uint64_t addr, ROBID rob_idx);
    bool                                has_unknown_older_store(uint64_t addr, ROBID rob_idx) const;
    bool                                should_wait_for_store_set(LQEntry& load_entry);
    int64_t                             find_dependent_store(uint32_t ssid);

    void                                detect_violation(LQEntry& load_entry);
    void                                detect_store_violations(SQEntry& st);
    void                                send_memory_order_flush(LQEntry& load_entry, SQEntry& store_entry);
    void                                wakeup_load(MInstPtr inst);
    void                                write_memory(u64 addr, u64 data, u32 width);
    void                                update_cache_line(u64 addr);
    static u64                          extract_load_data(const MemResp& resp, u64 addr, u32 width, u32 line_size);

private:
    RingQueue<MInstPtr>                 m_complete_q;
    std::deque<MInstPtr>                m_wb_pending;
    std::unordered_map<uint64_t, size_t> m_store_token_map;
    StoreSetPredictor                   m_ssp;
    ROB&                                m_rob;
    GCache&                             m_dcache;
    MultiLevelTLB&                      m_dtlb;
    Memory&                             m_mem;
    CycleScheduler&                     m_sched;
    ASyncQueue<FlushEvent>&             m_flush_req_q;
    std::vector<WakeupEvent>&           m_wakeup_bus;
    std::vector<LQEntry>&               lsu_ld_q;
    std::vector<SQEntry>&               lsu_st_q;
    LSUConfig                           m_conf;
    LSUStallType                        m_stall { NO_STALL };

    u32                                 m_issue_width;
    u32                                 m_load_q_size;
    u32                                 m_store_q_size;
    u32                                 m_idx { 0 };
    static u32                          lsu_idx;
    uint64_t                            m_ld_id { 0 };
    uint64_t                            m_st_id { 0 };

};

#endif
