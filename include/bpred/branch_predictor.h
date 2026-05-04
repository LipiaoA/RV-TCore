#ifndef RV_TCORE_BRANCHPREDICTOR_H
#define RV_TCORE_BRANCHPREDICTOR_H

#include <string>

#include "intf.h"
#include "btb.h"
#include "ras.h"
#include "bim.h"
#include "tage.h"

class BranchPredictor {

public:
    BranchPredictor(const BPConfig& config);
    ~BranchPredictor();

public:
    void                                update();

    void                                reset();
    branch_pred                         f1_pred(pc_t pc);
    branch_pred                         f2_pred(pc_t pc);
    TagePred                            f3_pred(pc_t pc);

private:
    u64                                 m_time;
    RAS                                 m_ras;
    BTB                                 m_ibtb;
    TAGE                                m_tage;
    BIM                                 m_bim;

    u32                                 m_fetch_width;

};

#endif
