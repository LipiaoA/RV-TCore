#ifndef RV_TCORE_DIRECTBTB_H
#define RV_TCORE_DIRECTBTB_H

#include "util.h"
#include "intf.h"
#include "config.h"

struct BTBEntry {
    bool valid = false;
    u64 tag = -1ull;
    pc_t target = -1ull;
    BrType type = BrType::NONE;
    u32 last_used_time = 0; // for LRU
};

struct BTBResp {
    bool hit = false;
    pc_t target = -1ull;
    BrType type = BrType::NONE;
};

class BTB {
    constexpr static u32 MAX_SET = 512;
    constexpr static u32 MAX_WAYS = 8;
    constexpr static u32 MAX_ENTRIES = MAX_SET * MAX_WAYS;
    constexpr static u32 MAX_TAG_BITS = 16;

public:
    BTB(u32 nsets, u32 ways, u32 tag_bits, u32 pc_lowdrop);
    BTB(const BTBConfig& config);
    ~BTB() {}
    BTB();
    BTBResp                                     lookup(pc_t pc, u32 time);

    void                                        update(pc_t pc, pc_t target, BrType type, u32 time);

    void                                        invalidate_all();
private:
    std::pair<u32, u64>                         index_and_tag(pc_t pc) const;

private:
    u32                                         m_nsets;
    u32                                         m_ways;
    u32                                         m_tagbits;
    u32                                         m_lowdrop;
    BTBEntry                                    m_sets[MAX_SET][MAX_WAYS];
};

#endif
