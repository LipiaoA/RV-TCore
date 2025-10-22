
#ifndef RV_TCORE_ROB_H
#define RV_TCORE_ROB_H

#include "intf.h"

class ReorderBuffer : public IRob {
public:
    ReorderBuffer(const std::string& n) : IRob(n) {}
    ~ReorderBuffer() {}
public:
    void tick() {}
};

#endif