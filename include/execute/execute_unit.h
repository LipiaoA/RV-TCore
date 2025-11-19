#ifndef RV_TCORE_EXECUTEUNIT_H
#define RV_TCORE_EXECUTEUNIT_H

#include "intf.h"

class ExecuteUnit : public IExecute {
public:
    ExecuteUnit(const std::string& n) : IExecute(n) {}
    ~ExecuteUnit() {}
public:
    void                                tick() {}
    void                                commit() {}
};

#endif