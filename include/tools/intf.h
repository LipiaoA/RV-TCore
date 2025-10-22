#ifndef TCORE_INTF_H
#define TCORE_INTF_H

#include <string>
#include <vector>

#include "rv_def.h"

class DevIntf {
public:
    DevIntf(const std::string &n): name(n) {}
    virtual ~DevIntf() = default;

    virtual void tick() = 0;                    // 组合逻辑评估
    virtual void commit() {}                    // 将 next->current 生效（子类覆盖）
    virtual void reset() {}                     // 复位
    virtual void build() {}

    virtual std::string info() const { return name; }
    const std::string name;
};

class IPredictor : public DevIntf {
public:
    IPredictor(const std::string& n) : DevIntf(n) {}
    virtual ~IPredictor() = default;
};

class ICommit  : public DevIntf {
public:
    ICommit(const std::string& n) : DevIntf(n) {}
    virtual ~ICommit() = default;
};

class IDecode : public DevIntf {
public:
    IDecode(const std::string& n) : DevIntf(n) {}
    virtual ~IDecode() = default;
};

class IDispatch : public DevIntf {
public:
    IDispatch(const std::string& n) : DevIntf(n) {}
    virtual ~IDispatch() = default;
};

class IExecute : public DevIntf {
public:
    IExecute(const std::string& n) : DevIntf(n) {}
    virtual ~IExecute() = default;
};


class IFetch : public DevIntf {
public:
    IFetch(const std::string& n) : DevIntf(n) {}
    virtual ~IFetch() = default;
};

class IIssue : public DevIntf {
public:
    IIssue(const std::string& n) : DevIntf(n) {}
    virtual ~IIssue() = default;
};

class IRename : public DevIntf {
public:
    IRename(const std::string& n) : DevIntf(n) {}
    virtual ~IRename() = default;
};

class IRob : public DevIntf {
public:
    IRob(const std::string& n) : DevIntf(n) {}
    virtual ~IRob() = default;
};


class ICore : public DevIntf {
public:
    ICore(const std::string& n) : DevIntf(n) {}
    virtual ~ICore() {
        for (auto &m : m_module) {
            delete m;
            m = nullptr;
        }
    };

public:
    void tick() {
        for(auto m : m_module) {
            m->tick();
        }
    }

    void commit() {
        for (auto m : m_module) {
            m->commit();
        }
    }
    void reset() {
        for (auto m : m_module) {
            m->reset();
        }
    };

protected:
    void add_module(DevIntf *module) {
        m_module.emplace_back(module);
    }

private:
    std::vector<DevIntf *> m_module;

};


#endif
