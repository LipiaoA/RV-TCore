
#ifndef RV_TCORE_RENAMEUNIT_H
#define RV_TCORE_RENAMEUNIT_H

#include "intf.h"

class RenameUnit : public IRename {
public:
    RenameUnit(const std::string& n) : IRename(n) {}
    ~RenameUnit() {}
public:
    void                                tick() {}
    void                                commit() {}
};

#endif