#ifndef RV_TCORE_FETCHUNIT_H
#define RV_TCORE_FETCHUNIT_H

#include "intf.h"
#include "icache.h"
#include "branch_predictor.h"

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

struct fetch_inst_t {
    pc_t pc { 0 };
    insn_t insn { 0 };
    bool valid { false };
    uint64_t fetch_id { 0 };  // 用于 squash 精确识别
};

struct fetch_packet_t {
    pc_t base_pc { 0 };                    // 起始 PC
    std::vector<fetch_inst_t> insts;       // insts.size() <= k_fetch_width
    bool valid { false };
    uint64_t fetch_id { 0 };
    // 预测相关元数据
    bool pred_taken { false };
    pc_t pred_target { 0 };
};

class FetchUnit : public IFetch {
public:
    FetchUnit(const std::string& n) : IFetch(n) {}
    ~FetchUnit() {}
public:
    void tick() {}
    void redirct(pc_t new_pc, uint64_t fetch_id = 0);

private:
    void fetch0();
    void fetch1();
    void fetch2();
    void fetch3();
    void fetch4();

    // utility
    static pc_t align_line(pc_t pc) { return (pc / ICache::k_line_bytes) * ICache::k_line_bytes; }

private:
    std::unique_ptr<ICache> m_icache;
    std::unique_ptr<BranchPredictor> m_bpu;
    uint64_t m_squash_id;               // squash 的阈值（>= 则丢弃）
    uint64_t m_next_fetch_id { 1 };     // 唯一 fetch id 分配器

    STATE_F0 pipe_reg_t<branch_pred_t> m_pred;
    STATE_F1 pipe_reg_t<pc_t> m_f1_req;
    STATE_F2 pipe_reg_t<bytes_t> m_f2_line;
    STATE_F3 pipe_reg_t<fetch_packet_t> m_f3_pkt;
    STATE_F4 pipe_reg_t<fetch_packet_t> m_f4_pkt;

    // small queue to hold completed packets before dispatch
    std::deque<fetch_packet_t> m_dispatch_q;
    size_t m_dispatch_q_capacity { 8 };


    bool m_icache_req_pending { false };
    pc_t m_pending_addr { 0 };
    uint32_t m_fetch_width;
    pc_t m_pc;
};

#endif