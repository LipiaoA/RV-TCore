
#ifndef RV_TCORE_PSEUDOLRU_H
#define RV_TCORE_PSEUDOLRU_H

#include <vector>

#include "util.h"

// ------------------------- Pseudo-LRU helper ---------------------------
// Tree-based PLRU for power-of-two ways. For non-power-of-two, fall back to full LRU.
class PseudoLRU {
public:
    PseudoLRU() {}
    void                            init(size_t ways);
    void                            touch(size_t way);
    size_t                          victim();
private:
    bool                            is_power_of_two(size_t x) { return x && ((x & (x - 1)) == 0); }
    size_t                          m_ways = 0;
    bool                            m_use_tree = false;
    std::vector<u8>                 m_tree_bits; // tree nodes
};

#endif
