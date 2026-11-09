#ifndef TCORE_BIM_H
#define TCORE_BIM_H

#include "util.h"
#include "intf.h"
#include "module_comm.h"
#include "config.h"

struct SatCounter {
    u8 val = 1; // weak not taken
    bool predict() const { return val >= 2; }
    void update(bool taken) {
        if (taken && val < 3) val++;
        if (!taken && val > 0) val--;
    }
};

class BIM {
    constexpr static size_t MAX_ENTRIES = 0x8000; // 32K

public:
    BIM(u32 entries = 0x4000, u32 histLen = 16, u32 fetchWidth = 4);
    BIM(const BIMConfig& config);

    // cond_mask 的第 i 位表示 base+i*4 是条件分支。只对这些槽查表并推进 GHR。
    branch_pred                         predict(pc_t base, u32 nslots, u32 cond_mask);
    bool                                peek(pc_t pc) const;
    void                                update(pc_t pc, bool taken, u64 ghr);
    void                                rollback(u64 ghr);
    void                                shift(bool taken);
    u64                                 ghr() const { return m_ghr; }
    u64                                 advance(u64 ghr, bool taken) const;

private:
    u32                                 m_entries;
    u32                                 m_histLen;
    u32                                 m_fetch_width;

    SatCounter                          m_table[MAX_ENTRIES];
    u64                                 m_ghr = 0;

    u64                                 hist_mask() const;
    u32                                 index(pc_t pc, u64 ghr) const;
};

#endif
