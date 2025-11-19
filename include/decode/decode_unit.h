
#ifndef RV_TCORE_DECODEUNIT_H
#define RV_TCORE_DECODEUNIT_H

#include "intf.h"

class DecodeUnit : public IDecode {
public:
    DecodeUnit(const std::string& n) : IDecode(n) {}
    ~DecodeUnit() {}
public:
    void                                tick() {}
    void                                commit() {}
};

#endif