
#include "branch_predictor.h"

BranchPredictor::BranchPredictor(const std::string& n)
 : IPredictor(n)
{
    size_t bht_entries = 2048;
    unsigned bhr_bits = 6;
    unsigned pc_low_bits = 6;
    unsigned ghr_bits = 8;
    bool chooser_table = false;
    m_hpred = new HybridPredictor(bht_entries, pc_low_bits, bhr_bits, ghr_bits, chooser_table);
}

BranchPredictor::BranchPredictor()
    : BranchPredictor("BranchPredictor")
{
}

branch_pred_t BranchPredictor::predict(u64 pc) {
    branch_pred_t pred;
    pred.valid = true;
    pred.taken = m_hpred->predict(pc);

    return pred;
}

void BranchPredictor::update(u64 pc, bool taken, pc_t target) {
    m_hpred->update(pc, taken);

    return;
}

BranchPredictor::~BranchPredictor() {
    delete_ptr(m_hpred);
}
