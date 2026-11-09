#include "btb.h"

BTB::BTB(u32 nsets, u32 ways, u32 tag_bits, u32 pc_lowdrop)
	: m_nsets(nsets),
	  m_ways(ways),
	  m_tagbits(tag_bits),
	  m_lowdrop(pc_lowdrop)
{ }

BTB::BTB(const BTBConfig& config)
	: m_nsets(config.nsets),
	  m_ways(config.ways),
	  m_tagbits(config.tag_bits),
	  m_lowdrop(config.pc_lowdrop)
{ }

BTB::BTB()
{
	m_nsets = 256;
	m_ways = 4;
	m_tagbits = 16;
	m_lowdrop = 2;
}

// lookup returns pair(hit, target)
BTBResp BTB::lookup(pc_t pc, u32 time) {
	auto [idx, tag] = index_and_tag(pc);
	auto &set = m_sets[idx];
	for (u32 w = 0; w < m_ways; ++w) {
		if (set[w].valid && set[w].tag == tag) {
			set[w].last_used_time = time;
			return {true, set[w].target, set[w].type};
		}
	}
	return {false, 0, BrType::NONE};
}

void BTB::update(pc_t pc, pc_t target, BrType type, u32 time) {
	auto [idx, tag] = index_and_tag(pc);
	auto &set = m_sets[idx];
	for (u32 w = 0; w < m_ways; ++w) {
		if (set[w].valid && set[w].tag == tag) {
			set[w].target = target;
			set[w].type = type;
			set[w].last_used_time = time;
			return;
		}
	}
	for (u32 w = 0; w < m_ways; ++w) {
		if (!set[w].valid) {
			set[w].valid = true;
			set[w].tag = tag;
			set[w].target = target;
			set[w].type = type;
			set[w].last_used_time = time;
			return;
		}
	}
	u32 lru = 0;
	u32 lru_time = set[0].last_used_time;
	for (u32 w = 1; w < m_ways; ++w) {
		if (set[w].last_used_time < lru_time) {
			lru = w;
			lru_time = set[w].last_used_time;
		}
	}
	set[lru].valid = true;
	set[lru].tag = tag;
	set[lru].target = target;
	set[lru].type = type;
	set[lru].last_used_time = time;
}

void BTB::invalidate_all() {
	memset(m_sets, 0, sizeof(m_sets));
}

std::pair<u32, u64> BTB::index_and_tag(pc_t pc) const
{
	u32 idx = (pc >> m_lowdrop) & (m_nsets - 1);
	u64 tag = (pc >> (m_lowdrop + __builtin_ctz(m_nsets))) & ((1ull<<m_tagbits) - 1);
	return {idx, tag};
}
