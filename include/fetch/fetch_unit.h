#ifndef RV_TCORE_FETCHUNIT_H
#define RV_TCORE_FETCHUNIT_H

#include "intf.h"
#include "icache.h"
#include "dcache.h"
#include "branch_predictor.h"
#include "module_comm.h"

/* -------------------- 数据结构：流水寄存器与包 -------------------- */
#define STATE_F0
#define STATE_F1
#define STATE_F2
#define STATE_F3
#define STATE_F4

template <typename T>
struct pipe_reg_t {
    T data{};
    bool valid { false };
    // reset helper
    void clear()
    {
        valid = false;
        data = T{};
    }
};

class FetchUnit : public IFetch {
public:
    FetchUnit(const std::string& n);
    ~FetchUnit();
public:
    void                                build();
    void                                tick();
    void                                commit() {}
    void                                redirct(pc_t new_pc, u64 fetch_id = 0);
    std::optional<FetchPacket>          try_dispatch();
    void                                dump_state() const;

    void                                set_fetch_decode_q(ASyncQueue<FetchPacket>* q);

private:
    void                                fetch0();
    void                                fetch1();
    void                                fetch2();
    void                                fetch3();
    void                                fetch4();

    // utility
    static pc_t align_line(pc_t pc) { return (pc / ICache::k_line_bytes) * ICache::k_line_bytes; }

private:
    std::unique_ptr<BranchPredictor>    m_bpu;

    u64                                 m_squash_id;               // squash 的阈值（>= 则丢弃）
    u64                                 m_next_fetch_id { 1 };     // 唯一 fetch id 分配器

    STATE_F0 pipe_reg_t<branch_pred_t>  m_pred;
    STATE_F1 pipe_reg_t<pc_t>           m_f1_req;
    STATE_F2 pipe_reg_t<bytes_t>        m_f2_line;
    STATE_F3 pipe_reg_t<FetchPacket>    m_f3_pkt;
    STATE_F4 pipe_reg_t<FetchPacket>    m_f4_pkt;

    size_t                              m_dispatch_q_capacity {8};

    bool                                m_icache_req_pending { false };
    pc_t                                m_pending_line_addr { 0 };
    u32                                 m_fetch_width;
    pc_t                                m_pc;
    ICache*                             m_icache;
    GCache*                             m_dcache;
    GCache*                             m_l2_cache;
    MultiLevelTLB*                      m_tlb;
    CycleScheduler*                     m_sched;

    OUTPUT ASyncQueue<FetchPacket>*     m_fetch_decode_q { nullptr };
};

#endif