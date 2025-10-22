
#ifndef RV_TCORE_DISPATCHUNIT_H
#define RV_TCORE_DISPATCHUNIT_H

#include "intf.h"

class DispatchUnit : public IDispatch {
public:
    DispatchUnit(const std::string& n) : IDispatch(n) {}
    ~DispatchUnit() {}
public:
    void tick() {}
};

#endif