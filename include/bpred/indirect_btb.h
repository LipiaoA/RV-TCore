#ifndef TCORE_INDIRECTBTB_H
#define TCORE_INDIRECTBTB_H

#include "rv_def.h"
#include "util.h"

struct IndirectEntry {
    bool        valid = false;
    u64         tag = 0; // includes folded history bits
    u64         target = 0;
    uint8_t     useful = 0; // 2-bit
    uint32_t    last_used = 0;
};

class IndirectBTB {
public:
    IndirectBTB(int nsets, int ways, int tagbits, int bhr_bits, int pc_lowdrop);

    std::pair<bool, u64>                        lookup(u64 pc, u64 bhr, uint32_t time);

    void                                        insert_or_update(u64 pc, u64 bhr, u64 target, bool correct, uint32_t time);

    void                                        invalidate_all();

private:
    std::pair<int, u64>                         index_and_tag(u64 pc, u64 bhr) const;

    int                                         m_nsets;
    int                                         m_ways;
    int                                         m_tagbits;
    int                                         m_bhr_bits;
    int                                         m_lowdrop;
    std::vector<std::vector<IndirectEntry>>     m_sets;
};

#endif
