#ifndef RV_TCORE_DIRECTBTB_H
#define RV_TCORE_DIRECTBTB_H

#include "util.h"

// ----------------------------- Direct BTB -------------------------------
struct DirectBTBEntry {
    bool valid = false;
    u64 tag = 0;
    u64 target = 0;
    uint8_t meta = 0; // optional
    u32 last_used_time = 0; // for LRU
};

class DirectBTB {
public:
    DirectBTB(int nsets, int ways, int tag_bits, int pc_lowdrop);
    // lookup returns std::pair(hit, target)
    std::pair<bool, u64>                        lookup(u64 pc, u32 time);

    void                                        insert(u64 pc, u64 target, u32 time);

    void                                        invalidate_all();
private:
    std::pair<int, u64>                         index_and_tag(u64 pc) const;

private:
    int                                         m_nsets;
    int                                         m_ways;
    int                                         m_tagbits;
    int                                         m_lowdrop; // how many low PC bits are ignored (alignment)
    std::vector<std::vector<DirectBTBEntry>>    m_sets;
};

#endif
