#ifndef RV_TCORE_FETCHUNIT_H
#define RV_TCORE_FETCHUNIT_H

#include "intf.h"

class FetchUnit : public IFetch {
public:
    FetchUnit(const std::string& n) : IFetch(n) {}
    ~FetchUnit() {}
public:
    void tick() {}
};

#endif