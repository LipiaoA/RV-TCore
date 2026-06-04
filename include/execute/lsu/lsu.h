#ifndef RV_TCORE_LSU_H
#define RV_TCORE_LSU_H

#include <vector>
#include <optional>
#include <algorithm>
#include <cstring>

#include "rv_def.h"
#include "module_comm.h"
#include "agu.h"
#include "memory/cache.h"
#include "memory/tlb.h"
#include "rob/rob.h"
#include "util.h"
#include "config.h"

static const int SSIT_SIZE = 1024;
static const int LFST_SIZE = 1024;

struct StoreSetPredictor {
    uint32_t SSIT[SSIT_SIZE];   // PC → SSID
    int64_t  LFST[LFST_SIZE];   // SSID → 最后一个 store_id

    StoreSetPredictor() {
        memset(SSIT, 0, sizeof(SSIT));
        for (auto& x : LFST) x = -1;
    }

    uint32_t getSSID(uint64_t pc) const
    {
        return SSIT[pc % SSIT_SIZE];
    }

    void mergeSSID(uint64_t load_pc, uint64_t store_pc)
    {
        uint32_t s1 = SSIT[load_pc % SSIT_SIZE];
        uint32_t s2 = SSIT[store_pc % SSIT_SIZE];
        uint32_t new_id = std::min(s1, s2);
        SSIT[load_pc % SSIT_SIZE] = new_id;
        SSIT[store_pc % SSIT_SIZE] = new_id;
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
    constexpr static u32 MAX_AGU_NUM = 4;
    typedef struct {
        bool valid { false };
        uint64_t addr;
        uint64_t data;
        uint64_t store_id;
        ROBID rob_idx {};
    } StData;
public:
    LSU(const LSUConfig& conf, const LSUDeps& deps);
    ~LSU();
    void                                build();

    bool                                dispatch(MInstPtr inst);
    void                                tick();
    void                                commit();
    void                                flush(const FlushEvent& event);
    void                                reset();

    bool                                commit_ready() const;
    MInstPtr                            commit_inst() const;
    void                                commit_ack();

    int                                 alloc_load_entry();
    int                                 alloc_store_entry();
    void                                replay(LQEntry& load_entry);
    void                                replay(SQEntry& store_entry);
    bool                                checkViolation(uint64_t addr, ROBID rob_idx);

private:
    void                                schedule();
    void                                exec_load(LQEntry& load_entry);
    void                                exec_store(SQEntry& store_entry);

    std::optional<uint64_t>             forward(uint64_t addr, ROBID rob_idx);
    int                                 find_dependent_store(uint32_t ssid);

    void                                detect_violation(LQEntry& load_entry);
private:
    std::vector<LQEntry>                m_load_q;
    std::vector<SQEntry>                m_store_q;
    RingQueue<MInstPtr>                 m_complete_q;
    FixedStorage<AGU, MAX_AGU_NUM>      m_agus;
    StoreSetPredictor                   m_ssp;
    ROB&                                m_rob;
    GCache&                             m_dcache;
    MultiLevelTLB&                      m_dtlb;
    LSUConfig                           m_conf;
    CycleScheduler                      m_sched;
    LSUStallType                        m_stall { NO_STALL };

    RingQueue<StData>                   m_mshr_buffer;
    u32                                 m_issue_width;
    u32                                 m_load_q_size;
    u32                                 m_store_q_size;
    u32                                 m_agus_num;
    u32                                 m_mshr_buffer_size;
    u32                                 m_idx { 0 };
    static u32                          lsu_idx;
};

#endif
