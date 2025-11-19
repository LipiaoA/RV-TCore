#include "hybrid_predictor.h"

HybridPredictor::HybridPredictor(size_t bht_entries,
				unsigned pc_low_bits,
				unsigned bhr_bits,
				unsigned ghr_bits,
				bool chooser_as_table,
				size_t chooser_entries)
	: m_local(bht_entries, pc_low_bits, bhr_bits),
	  m_global(ghr_bits), m_pc_low_bits(pc_low_bits),
	  m_chooser_as_table(chooser_as_table)
{
	if (chooser_as_table) {
		m_chooser_table_size = chooser_entries;
		m_chooser_table.assign(m_chooser_table_size, Sat2(1));
	} else {
		m_chooser_single = SatN(2);
		m_chooser_single.val = 1;
	}
	m_total = m_correct = m_local_correct = m_global_correct = m_chooser_updates = 0;
}

bool HybridPredictor::predict(u64 pc) {
	bool local_pred, global_pred;

	return pred(pc, local_pred, global_pred);
}

void HybridPredictor::update(u64 pc, bool taken) {
	int local_idx;
	u32 bhr;
	bool local_pred = m_local.predict(pc, local_idx, bhr);
	bool global_pred = m_global.predict();
	bool choose_global = choose(pc);
	bool final_pred = choose_global ? global_pred : local_pred;

	++m_total;
	if (final_pred == taken) ++m_correct;
	if (local_pred == taken) ++m_local_correct;
	if (global_pred == taken) ++m_global_correct;

	m_local.update(pc, taken);
	m_global.update(taken);

	if (local_pred != global_pred) {
		if (global_pred == taken)
		adjust_chooser(pc, true);
		else if (local_pred == taken)
		adjust_chooser(pc, false);
	}
}

bool HybridPredictor::pred(u64 pc, bool &local_pred, bool &global_pred) {
	int local_idx;
	u32 bhr;
	local_pred = m_local.predict(pc, local_idx, bhr);
	global_pred = m_global.predict();
	bool choose_global = choose(pc);
	return choose_global ? global_pred : local_pred;
}

bool HybridPredictor::choose(u64 pc) const {
	if (m_chooser_as_table) {
		size_t idx = chooser_index(pc);
		return m_chooser_table[idx].taken(); // >=2 => prefer global
	} else {
		return m_chooser_single.predict_high();
	}
}

void HybridPredictor::adjust_chooser(u64 pc, bool toward_global) {
	if (m_chooser_as_table) {
		size_t idx = chooser_index(pc);
		m_chooser_table[idx].update(toward_global);
	} else {
		if (toward_global) m_chooser_single.inc();
		else m_chooser_single.dec();
	}
	++m_chooser_updates;
}

size_t HybridPredictor::chooser_index(u64 pc) const {
	if (!m_chooser_as_table) return 0;
	return (pc >> 2) & (m_chooser_table_size - 1);
}

double HybridPredictor::accuracy() const { return m_total ? (double)m_correct / m_total : 0.0; }
double HybridPredictor::local_acc() const { return m_total ? (double)m_local_correct / m_total : 0.0; }
double HybridPredictor::global_acc() const { return m_total ? (double)m_global_correct / m_total : 0.0; }
