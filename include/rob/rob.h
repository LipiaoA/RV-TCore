
#ifndef RV_TCORE_ROB_H
#define RV_TCORE_ROB_H

#include "intf.h"

class ROB : public IRob {
public:
    ROB(const std::string& n) : IRob(n) {}
    ~ROB() {}
public:
    void                                tick() {}
    void                                commit() {}
};

#endif