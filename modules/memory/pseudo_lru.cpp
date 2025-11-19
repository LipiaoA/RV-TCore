#include "pseudo_lru.h"

void PseudoLRU::init(size_t ways) {
    m_ways = ways;
    if (is_power_of_two(ways)) {
        m_use_tree = true;
        m_tree_bits.assign(ways-1, 0);
    } else {
        m_use_tree = false;
    }
}

void PseudoLRU::touch(size_t way) {
    if (!m_use_tree) { // nothing
        // users will maintain true LRU externally
        return;
    }
    // update tree bits from root to leaf
    size_t idx = 0;
    size_t span = m_ways;
    while (span > 1) {
        span /= 2;
        if (way < idx + span) { // go left
            m_tree_bits[idx] = 1;
            idx = idx * 2 + 1; // set to prefer right next time
        } else {
            m_tree_bits[idx] = 0;
            idx = idx * 2 + 2;
            way -= span;
        }
    }
    return;
}

size_t PseudoLRU::victim()
{
    if (!m_use_tree) return SIZE_MAX;
    // walk tree to find victim
    size_t idx = 0;
    size_t way = 0;
    size_t span = m_ways;
    while (span > 1) {
        span /= 2;
        if (m_tree_bits[idx] == 0) { // go left
            // mark to prefer left next time
            m_tree_bits[idx] = 1;
            idx = idx * 2 + 1;
        } else {
            m_tree_bits[idx] = 0;
            idx = idx * 2 + 2;
            way += span;
        }
    }
    return way;
}

