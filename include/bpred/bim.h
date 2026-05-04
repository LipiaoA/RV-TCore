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

    branch_pred                         predict(pc_t pc);
    void                                update(pc_t pc, bool taken);
    void                                rollback(u64 ghr);

private:
    u32                                 m_entries;
    u32                                 m_histLen;
    u32                                 m_fetch_width;

    SatCounter                          m_table[MAX_ENTRIES];
    u64                                 m_ghr = 0;

    u32                                 index(pc_t pc) const;
};

#endif
