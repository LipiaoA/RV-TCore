#ifndef RV_TCORE_LSU_H
#define RV_TCORE_LSU_H

#include <vector>
#include <optional>
#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <array>

#include "rv_def.h"
#include "module_comm.h"
#include "memory/cache.h"
#include "memory/tlb.h"
#include "memory/mem.h"
#include "rob/rob.h"
#include "util.h"
#include "config.h"
#include "perf/perf_def.h"
#include "store_buffer.h"
#include "store_predictor.h"


enum LSUStallType {
    NO_STALL = 0,
    TLB_PORT_FULL = 1,
    PTW_FULL = 2,
    DCACHE_PORT_FULL = 3,
    MSHR_FULL = 4,
    STORE_BUFFER_FULL = 5
};

struct LSUExeSlot {
    bool valid { false };
    size_t ldq_idx { 0 };
    u64 vaddr { 0 };
    u64 paddr { 0 };
    u8 byte_mask { 0 };
    bool tlb_miss { false };
    bool mem_issued { false };
};

struct LSUS1Slot {
    bool valid { false };
    size_t ldq_idx { 0 };
    u64 vaddr { 0 };
    u64 paddr { 0 };
    u8 byte_mask { 0 };
    bool tlb_miss { false };
    bool killed { false };
    bool forward_val { false };
    size_t forward_stq_idx { 0 };
    bool mem_issued { false };
};

struct LSUWBSlot {
    bool valid { false };
    uint64_t load_id { 0 };
    u64 data { 0 };
    u32 cycles_left { 0 };
};

struct PendingMemResp {
    uint64_t load_id { 0 };
    uint64_t req_gen { 0 };
    u64 addr { 0 };
    u32 width { 0 };
    MemResp resp {};
};

struct PendingStoreResp {
    uint64_t store_id { 0 };
};

class LSU {
public:
    LSU(const LSUConfig& conf, const LSUDeps& deps);
    ~LSU();
    void                                build();

    bool                                can_dispatch(MInstPtr inst) const;
    bool                                can_alloc(MInstPtr inst) const;
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

    LSUStallType                        stall_type() const { return m_stall; }

    // 性能统计：在途未完成 Load / Store 资源受限
    bool                                has_outstanding_load() const;
    bool                                store_resource_limited() const;

    int                                 alloc_load_entry();
    int                                 alloc_store_entry();
    void                                replay(LQEntry& load_entry);
    void                                replay(SQEntry& store_entry);
    void                                replay_store_half(ROBID rid, bool kill_sta, bool kill_std);
    // 1A: load 错推测轻量恢复（原地 replay + LoadDepKill，不 flush）
    void                                recover_load_misspec(LQEntry& load_entry, SQEntry* store_entry);
    bool                                check_stq_violation(uint64_t addr, ROBID rid, u8 byte_mask) const;

    SQEntry*                            find_sq_for_std(MInstPtr inst);
    SQEntry*                            find_store_by_id(uint64_t store_id);
    LQEntry*                            find_load_by_id(uint64_t load_id);
    void                                on_store_operands_updated(SQEntry& st);
    SQEntry*                            find_store_by_rid(ROBID rid);
    LQEntry*                            find_load_by_rid(ROBID rid);

private:
    static size_t                       wrap_inc(size_t idx, size_t cap);
    static size_t                       wrap_dec(size_t idx, size_t cap);
    static u8                           gen_byte_mask(u64 addr, u32 width);
    static bool                         dword_addr_match(u64 a, u64 b);
    static bool                         mask_overlap(u8 a, u8 b);
    static bool                         mask_contains(u8 container, u8 sub);
    static u64                          apply_store_data(u64 store_addr, u64 store_data, u32 store_width,
                                                         u64 load_addr, u32 load_width);

    bool                                ldq_full() const;
    bool                                stq_full() const;
    void                                update_live_store_mask();

    void                                wb();
    void                                rsp();
    void                                lc();
    void                                ldx();
    void                                stlb();
    void                                iss();
    void                                sta(SQEntry& st);

    void                                fire_load(size_t ldq_idx);
    void                                do_ldx(LSUExeSlot& slot);
    void                                lcam(LSUS1Slot& slot);
    void                                issue_mem(LQEntry& entry, u64 pa, u8 byte_mask);
    void                                complete_load(uint64_t load_id, uint64_t data);
    void                                enqueue_wb(uint64_t load_id, u64 data, u32 delay);

    struct LoadPick {
        bool ok { false };
        bool replay { false };
        bool sb_wait { false };
        bool forwarded { false };
        bool from_sb { false };
        size_t stq_idx { 0 };
        u64 data { 0 };
    };
    LoadPick                            resolve_load(const LQEntry& entry, u64 mem_data, bool have_mem) const;
    void                                issue_sb();
    void                                note_sb_write(const SBEntry& written);
    void                                retire_stq_head();
    std::optional<u64>                  try_forward_sb(u64 addr, u8 load_mask, u32 load_width) const;
    bool                                sb_blocks_load(u64 addr, u8 load_mask) const;


    LQEntry*                            find_load_by_ldq_idx(size_t idx);
    SQEntry*                            find_store_by_stq_idx(size_t idx);

    std::optional<std::pair<size_t, u64>> try_forward(u64 addr, u8 load_mask,
                                                      const LQEntry& load, bool can_forward) const;
    size_t                              pick_fwd_stq(u64 addr, u8 load_mask, const LQEntry& load) const;
    bool                                is_spec_load(u64 addr, u8 byte_mask, ROBID rid) const;
    bool                                load_blocked(size_t ldq_idx) const;
    void                                block_load(size_t ldq_idx);
    bool                                store_set_block(LQEntry& load_entry);
    void                                clear_store_set_wait(LQEntry& load_entry);
    void                                train_store_set(MInstPtr load_uop, SQEntry& store_entry);

    void                                check_mem_order(SQEntry& st);
    void                                flush_order(LQEntry& load_entry, SQEntry& store_entry);
    void                                emit_load_dep_kill(MInstPtr load_uop);
    void                                wakeup_load(MInstPtr inst, bool speculative);
    void                                replay_load(LQEntry& entry, bool sb_wait);
    static u64                          extract_load_data(const MemResp& resp, u64 addr, u32 width, u32 line_size);
    bool                                grab_mem_port();
    void                                dump_state() const;


    RingQueue<MInstPtr>                 m_complete_q;
    std::deque<PendingMemResp>          m_pending_mem_resps;
    std::deque<PendingStoreResp>        m_pending_store_resps;
    std::deque<LSUWBSlot>               m_wb_pipe;
    std::vector<LSUExeSlot>             m_exe_slots;
    std::vector<LSUS1Slot>              m_s1_slots;
    std::deque<size_t>                  m_pending_sta_tlb;
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

    size_t                              m_ldq_head { 0 };
    size_t                              m_ldq_tail { 0 };
    size_t                              m_stq_head { 0 };
    size_t                              m_stq_tail { 0 };
    size_t                              m_stq_commit_head { 0 };
    size_t                              m_stq_execute_head { 0 };
    uint64_t                            m_live_store_mask[2] { 0, 0 };
    std::vector<bool>                   m_block_load_mask;
    std::vector<bool>                   m_p1_block_load_mask;
    bool                                m_mem_port_busy { false };
    bool                                m_tlb_port_busy { false };
    StoreBuffer                         m_store_buffer;
    uint64_t                            m_sb_epoch { 0 };
    PerfStats&                          m_stats;
};

#endif
