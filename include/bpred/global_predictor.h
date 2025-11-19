#ifndef RV_TCORE_GLOBALPREDICTOR_H
#define RV_TCORE_GLOBALPREDICTOR_H

#include "intf.h"
#include "bht.h"

class GlobalPredictor {
public:
    GlobalPredictor(unsigned ghr_bits = 8)
        : m_ghr_bits(ghr_bits), m_ghr(0)
    {
        m_pht_size = 1ull << ghr_bits;
        m_pht.assign(m_pht_size, Sat2(2));
    }

    bool predict() const { return m_pht[m_ghr].taken(); }

    void update(bool taken) {
        m_pht[m_ghr].update(taken);
        m_ghr = ((m_ghr << 1) | (taken ? 1 : 0)) & ((1u << m_ghr_bits) - 1u);
    }

private:
    unsigned                                m_ghr_bits;
    unsigned                                m_ghr;
    size_t                                  m_pht_size;
    std::vector<Sat2>                       m_pht;
};

#endif
