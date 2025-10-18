#ifndef TCORE_CORE_H
#define TCORE_CORE_H

#include "util.h"
#include "intf.h"

class Core : public DevIntf {
public:
    Core(const std::string &n) : DevIntf(n) {}
    ~Core() {}
public:
    void eval() {}

};

#endif
