#ifndef RV_TCORE_FETCHUNIT_H
#define RV_TCORE_FETCHUNIT_H

#include <deque>
#include <optional>

#include "intf.h"
#include "branch_predictor.h"
#include "icache.h"
#include "module_comm.h"
#include "config.h"

class GCache;
class MultiLevelTLB;
class CycleScheduler;

/* -------------------- 数据结构：流水寄存器与包 -------------------- */
#define STATE_F0
#define STATE_F1
#define STATE_F2
#define STATE_F3
#define STATE_F4
#define STATE_F5

template <typename T>
struct pipe_reg_t {
    T data{};
    bool valid { false };
    void clear()
    {
        valid = false;
        data = T{};
    }
};

typedef struct {
    pc_t vpc { 0 };
    u32 group_idx { 0 };
    u32 bank_idx { 0 };
    bool valid { false };
    void clear()
    {
        valid = false;
        vpc = 0;
        group_idx = 0;
        bank_idx = 0;
    }
} FetchState;

class FetchUnit : public IFetch {
    constexpr static u32 MAX_STATES = 5;
    constexpr static u32 MAX_FETCH_WIDTH = 4;
    constexpr static u32 MAX_INST_COUNT = 32;
    enum {
        F0 = 0,
        F1 = 1,
        F2 = 2,
        F3 = 3,
        F4 = 4,
    };
    enum {RES_F1 = 0, RES_F2 = 1, RES_F3 = 2, RES_F4 = 3, RES_F5 = 4, RES_F6 = 5, RES_F7 = 6, RESET_PC = 7};
    typedef struct {
        /* 4 banks*/
        bool valid { false };
        u32 bmask;
        pc_t line_addr { 0 };
        u8 data[64];
        void clear()
        {
            valid = false;
            bmask = 0;
            line_addr = 0;
            memset(data, 0, sizeof(data));
        }
    } LineBuffer;
    typedef struct {
        bool valid { false };
        pc_t pc { 0 };
    } RedirectPC;

    typedef struct {
        pc_t pc { 0 };
        u32 insn { 0 };
        u64 fetch_id { 0 };
    } F2Inst;
public:
    FetchUnit(const FetchConfig& conf, const IFetchDeps& deps);
    ~FetchUnit() override;
public:
    void                                build() override;
    void                                tick() override;
    void                                commit() override;
    void                                redirct(pc_t new_pc, u64 fetch_id = 0);
    void                                dump_state() const;

private:
    void                                flush();
    void                                fetch0();
    void                                fetch1();
    void                                fetch2();
    void                                fetch3();
    void                                fetch4();
    void                                fetch5();
    pc_t                                next_pc();
    u32                                 align_group(pc_t pc) const;
    u32                                 align_bank(pc_t pc) const;
    u32                                 bank_idx(pc_t pc) const;
    u32                                 group_idx(pc_t pc) const;
private:
    CycleScheduler&                     m_sched;
    ICache&                             m_icache;
    BranchPredictor&                    m_bpu;
    OUTPUT ASyncQueue<FetchPacket>&     m_fetch_decode_q;
    FlushEvent&                         m_flush_event;

    u64                                 m_squash_id;

    LineBuffer                          m_line_buffer;
    std::queue<F2Inst>                  m_inst_buffer;

    STATE_F1 branch_pred                m_f1_pred;
    STATE_F2 branch_pred                m_f2_pred;
    STATE_F3 branch_pred                m_f3_bpred;
    STATE_F3 TagePred                   m_f3_pred[MAX_FETCH_WIDTH];

    STATE_F1 IFetchReq                  m_ifetch_req;


    STATE_F2 pipe_reg_t<bytes_t>        m_f2_line;
    STATE_F3 pipe_reg_t<FetchPacket>    m_f3_pkt;
    STATE_F4 pipe_reg_t<FetchPacket>    m_f4_pkt;
    STATE_F5 pipe_reg_t<FetchPacket>    m_f5_pkt;

    RedirectPC                          m_redirect_pc[8];


    FetchState                          m_state[MAX_STATES];
    size_t                              m_fetch_decode_cap {8};

    bool                                m_icache_req_pending { false };
    pc_t                                m_pending_line_addr { 0 };
    u32                                 m_fetch_width;
    pc_t                                m_pc;

    u32                                 m_group_size;
    u32                                 m_bank_size {16};
    u32                                 m_bank_count { 4 };
    u32                                 m_group_count { 2 };
    u32                                 m_epoch;
    u64                                 m_fetch_id { 1 };

};

#endif
