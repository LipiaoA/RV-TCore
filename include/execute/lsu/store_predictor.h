#ifndef RV_TCORE_STORE_PREDICTOR_H
#define RV_TCORE_STORE_PREDICTOR_H

#include <cstring>

#include "rv_def.h"
#include "config.h"

static const int SSIT_SIZE = 1024;
static const int LFST_SIZE = 1024;
static const size_t LSQ_MASK_WORDS = 2; // up to 128 STQ entries

class StoreSetPredictor
{
public:
    StoreSetPredictor();
    ~StoreSetPredictor();

    u32                                 get_ssid(u64 pc) const { return m_ssit[pc % SSIT_SIZE]; }
    u32                                 get_or_create_ssid(u64 pc);
    u32                                 merge_ssid(u64 load_pc, u64 store_pc);

    // LFST 写入/清除的是 stq_idx；空槽用 -1。
    void                                update_lfst(u32 ssid, u64 store_id);
    u64                                 get_lfst(u32 ssid) const;
    void                                clear_lfst(u32 ssid, u64 store_id);
    u32                                 ssid_idx(u64 pc) const;

private:
    u32                                 m_ssit[SSIT_SIZE];
    u64                                 m_lfst[LFST_SIZE];
    u32                                 m_next_id { 1 };
};

#endif
