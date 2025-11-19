#ifndef TCORE_CORE_H
#define TCORE_CORE_H

#include "util.h"
#include "intf.h"

class Core : public ICore {
public:
    Core(const std::string &n) : ICore(n) {
    }
    virtual ~Core() {}
public:
    virtual void                            build();
private:

};

#endif
