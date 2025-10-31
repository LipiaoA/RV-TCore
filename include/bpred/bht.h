
#ifndef TCORE_BHT_H
#define TCORE_BHT_H

class BHT {
public:
    BHT(size_t entries = 1024, unsigned bhr_bits = 4)
        : m_entries(entries), m_bhr_bits(bhr_bits) {
        m_mask = (1u << bhr_bits) - 1u;
        m_table.assign(entries, 0);
    }

    // 由 PC 计算 BHT 索引
    size_t index_from_pc(uint64_t pc) const {
        if ((m_entries & (m_entries - 1)) == 0)
            return (pc >> 2) & (m_entries - 1); // power of two
        else
            return (pc >> 2) % m_entries;
    }

    // 读取 BHR
    uint32_t get_bhr(uint64_t pc) const {
        return m_table[index_from_pc(pc)] & m_mask;
    }

    // 更新 BHR（左移+插入新结果）
    void update_bhr(uint64_t pc, bool taken) {
        size_t idx = index_from_pc(pc);
        uint32_t bhr = m_table[idx] & m_mask;
        bhr = ((bhr << 1) | (taken ? 1 : 0)) & m_mask;
        m_table[idx] = bhr;
    }

    size_t size() const { return m_entries; }
    unsigned bhr_bits() const { return m_bhr_bits; }

private:
    size_t m_entries;
    unsigned m_bhr_bits;
    uint32_t m_mask;
    vector<uint32_t> m_table;
};

#endif
