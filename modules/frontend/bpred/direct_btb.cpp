#include "direct_btb.h"

DirectBTB::DirectBTB(int nsets, int ways, int tag_bits, int pc_lowdrop)
	: m_nsets(nsets), m_ways(ways), m_tagbits(tag_bits), m_lowdrop(pc_lowdrop)
{
	m_sets.resize(nsets);
	for (int i = 0; i < nsets; i++) {
		m_sets[i].resize(ways);
	}
}

// lookup returns pair(hit, target)
std::pair<bool,u64> DirectBTB::lookup(u64 pc, uint32_t time) {
	auto [idx, tag] = index_and_tag(pc);
	auto &set = m_sets[idx];
	for (int w=0; w<m_ways; ++w) {
		if (set[w].valid && set[w].tag==tag) {
			set[w].last_used_time = time;
			return {true, set[w].target};
		}
	}
	return {false, 0};
}

void DirectBTB::insert(u64 pc, u64 target, u32 time) {
	auto [idx, tag] = index_and_tag(pc);
	auto &set = m_sets[idx];
	// look for invalid slot
	for (int w = 0; w < m_ways; ++w) {
		if (!set[w].valid) {
			set[w].valid = true;
			set[w].tag = tag;
			set[w].target = target;
			set[w].last_used_time = time;
			return;
		}
	}
	// replace LRU
	int lru = 0; uint32_t lru_time = set[0].last_used_time;
	for (int w = 1; w < m_ways; ++w) {
		if (set[w].last_used_time < lru_time) {
			lru = w;
			lru_time = set[w].last_used_time;
		}
	}
	set[lru].tag = tag;
	set[lru].target = target;
	set[lru].last_used_time = time;
}

void DirectBTB::invalidate_all() {
	for (auto &set : m_sets) for (auto &e : set) e = DirectBTBEntry();
}


std::pair<int, u64> DirectBTB::index_and_tag(u64 pc) const {
	u64 idx = (pc >> m_lowdrop) & (m_nsets-1);
	u64 tag = (pc >> (m_lowdrop + __builtin_ctz(m_nsets))) & ((1ull<<m_tagbits)-1);
	return {(int)idx, tag};
}
