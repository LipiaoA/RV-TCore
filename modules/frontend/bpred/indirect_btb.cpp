
#include "indirect_btb.h"

IndirectBTB::IndirectBTB(int nsets, int ways, int tagbits, int bhr_bits, int pc_lowdrop):
    m_nsets(nsets), m_ways(ways), m_tagbits(tagbits), m_bhr_bits(bhr_bits), m_lowdrop(pc_lowdrop) {
    m_sets.resize(nsets);
    for (int i=0;i<nsets;i++) m_sets[i].resize(ways);
}

std::pair<bool,u64> IndirectBTB::lookup(u64 pc, u64 bhr, uint32_t time) {
    auto [idx, tag] = index_and_tag(pc, bhr);
    auto &set = m_sets[idx];
    for (int w=0; w<m_ways; ++w) {
        if (set[w].valid && set[w].tag==tag) {
            set[w].last_used = time;
            // simple confidence check
            if (set[w].useful >= 1) return {true, set[w].target};
            else return {false, 0};
        }
    }
    return {false,0};
}

void IndirectBTB::insert_or_update(u64 pc, u64 bhr, u64 target, bool correct, uint32_t time) {
    auto [idx, tag] = index_and_tag(pc, bhr);
    auto &set = m_sets[idx];
    // update if exists
    for (int w=0; w<m_ways; ++w) {
        if (set[w].valid && set[w].tag==tag) {
            set[w].target = target;
            if (correct) { if (set[w].useful<3) ++set[w].useful; }
            else { if (set[w].useful>0) --set[w].useful; }
            set[w].last_used=time;
            return;
        }
    }
    // insert: find invalid
    for (int w=0; w<m_ways; ++w) if (!set[w].valid) {
        set[w].valid=true; set[w].tag=tag; set[w].target=target; set[w].useful = correct?1:0; set[w].last_used=time; return;
    }
    // replacement: pick least useful, then LRU
    int victim=0; int bestscore=1000000; uint32_t lru_time=UINT32_MAX;
    for (int w=0; w<m_ways; ++w) {
        int score = set[w].useful*1000 + (int)set[w].last_used; // prefer low useful, low last_used
        if (score < bestscore) { victim=w; bestscore=score; lru_time=set[w].last_used; }
    }
    set[victim].tag=tag; set[victim].target=target; set[victim].useful = correct?1:0; set[victim].last_used=time;
}

void IndirectBTB::invalidate_all() {
    for (auto &set : m_sets) for (auto &e : set) e = IndirectEntry();
}

std::pair<int,u64> IndirectBTB::index_and_tag(u64 pc, u64 bhr) const {
    // compute folded bhr signature
    u64 bhr_sig = xor_fold(bhr, std::min(m_bhr_bits, 16)); // limit fold bits
    u64 idx = ((pc >> m_lowdrop) ^ (bhr_sig)) & (m_nsets-1);
    u64 tag = ((pc >> (m_lowdrop + __builtin_ctz(m_nsets))) ^ (bhr_sig << 3)) & ((1ull<<m_tagbits)-1);
    return {(int)idx, tag};
}
