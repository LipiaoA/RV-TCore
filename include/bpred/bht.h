
#ifndef TCORE_BHT_H
#define TCORE_BHT_H

class BHT {
public:
    BHT(size_t entries = 1024, unsigned bhr_bits = 4)
        : m_entries(entries), m_bhr_bits(bhr_bits) {
        m_mask = (1u << bhr_bits) - 1u;
        m_table.assign(entries, 0);
    }

    size_t index_from_pc(u64 pc) const {
        if ((m_entries & (m_entries - 1)) == 0)
            return (pc >> 2) & (m_entries - 1); // power of two
        else
            return (pc >> 2) % m_entries;
    }

    u32 get_bhr(u64 pc) const {
        return m_table[index_from_pc(pc)] & m_mask;
    }

    void update_bhr(u64 pc, bool taken) {
        size_t idx = index_from_pc(pc);
        u32 bhr = m_table[idx] & m_mask;
        bhr = ((bhr << 1) | (taken ? 1 : 0)) & m_mask;
        m_table[idx] = bhr;
    }

    size_t                          size() const { return m_entries; }
    unsigned                        bhr_bits() const { return m_bhr_bits; }

private:
    size_t                          m_entries;
    unsigned                        m_bhr_bits;
    u32                             m_mask;
    std::vector<u32>                m_table;
};

#endif
