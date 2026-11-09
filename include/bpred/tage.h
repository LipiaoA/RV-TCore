#ifndef TCORE_TAGE_H
#define TCORE_TAGE_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"

class FoldedHistory {
public:
    FoldedHistory(u32 olen, u32 clen)
        : m_olen(olen), m_clen(clen) {}
    FoldedHistory() = default;

    void update(bool taken)
    {
        m_val = ((m_val << 1) | taken) ^ (taken << (m_olen % m_clen));
        m_val ^= (m_val >> m_clen);
        m_val &= (1 << m_clen) - 1;
    }

    void reset() { m_val = 0; }

    u64 value() const { return m_val; }

private:
    u32                 m_olen;
    u32                 m_clen;
    u64                 m_val = 0ull;
};

typedef struct TageEntry {
    u16 tag = 0;
    i8 ctr = 0;  // -4..+3
    u8 u = 0;
    bool predict() const { return ctr >= 0; }

    void update(bool taken)
    {
        if (taken && ctr < 3) ctr++;
        if (!taken && ctr > -4) ctr--;
    }
} TageEntry;

struct TageTable {
    constexpr static u32 MAX_ENTRIES = 2048;
    u32 hist_len;
    u32 idx_bits;
    u32 tag_bits;
    u32 size;
    u16 tag_mask;
    TageEntry entries[MAX_ENTRIES];
    FoldedHistory fold_idx;
    FoldedHistory fold_tag1;
    FoldedHistory fold_tag2;

    TageTable(u32 sz, u32 hlen, u32 tag_bits, u32 idx_bits)
        : hist_len(hlen),
          idx_bits(idx_bits),
          tag_bits(tag_bits),
          size(sz),
          tag_mask(static_cast<u16>((1u << tag_bits) - 1)),
          fold_idx(hlen, idx_bits),
          fold_tag1(hlen, tag_bits),
          fold_tag2(hlen, tag_bits - 1) {}
    TageTable() = default;
};

struct TagePred
{
    bool valid = false;
    bool final_pred = false;
    int provider = -1;
    int alt_provider = -1;
    bool alt_pred = false;
};

class TAGE {
    constexpr static u32 MAX_TABLES = 8;
public:
    TAGE(const TageConfig &cfg);

    TagePred                                predict(u64 pc);
    void                                    update(u64 pc, bool taken, TagePred &pred);
    void                                    reset();

private:
    TageTable                               m_tables[MAX_TABLES];
    u64                                     m_ghr = 0ull;
    u32                                     m_ntables;

    u32                                     index(i32 t, u64 pc) const;
    u16                                     ctag(i32 t, u64 pc) const;
};

#endif