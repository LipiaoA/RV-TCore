#ifndef TCORE_INTF_H
#define TCORE_INTF_H

#include <string>

#include "rv_def.h"

class DevIntf {
public:
    DevIntf(const std::string &n): name(n) {}
    virtual ~DevIntf() = default;

    virtual void eval() = 0;                    // 组合逻辑评估
    virtual void commit() {}                    // 将 next->current 生效（子类覆盖）
    virtual void reset() {}                     // 复位

    virtual std::string info() const { return name; }
    const std::string name;
};

#endif
