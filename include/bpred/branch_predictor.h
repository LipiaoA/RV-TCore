#ifndef RV_TCORE_BRANCHPREDICTOR_H
#define RV_TCORE_BRANCHPREDICTOR_H

#include "intf.h"
#include "hybrid_predictor.h"

struct branch_pred_t {
    bool valid { false };
    bool taken { false };
    pc_t target { 0 };
};

class BranchPredictor : public IPredictor {

public:
    BranchPredictor(const std::string& n);
    ~BranchPredictor();

public:
    void                                tick() override {}
    void                                commit() override {}
    void                                build() override {}
    branch_pred_t                       predict(pc_t pc);
    void                                update(pc_t pc, bool taken, pc_t target);

private:
    /* hybrid predictor */
    HybridPredictor*                    m_hpred;
};

#endif
