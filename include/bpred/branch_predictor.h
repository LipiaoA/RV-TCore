#ifndef RV_TCORE_BRANCHPREDICTOR_H
#define RV_TCORE_BRANCHPREDICTOR_H

#include "intf.h"

class BranchPredictor : public IPredictor {
public:
    BranchPredictor(const std::string& n) : IPredictor(n) {}
    ~BranchPredictor() {}
public:
    void tick() {}
};

#endif