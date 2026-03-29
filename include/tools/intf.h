#ifndef TCORE_INTF_H
#define TCORE_INTF_H

#include <string>
#include <vector>

#include "rv_def.h"
#include "util.h"

class DevIntf {
public:
    DevIntf(const std::string &n): m_name(n) {}
    DevIntf(const DevIntf&) = delete;
    DevIntf() : m_name("unknown") {}
    virtual ~DevIntf() = default;

    virtual void tick() = 0;                    // 组合逻辑评估
    virtual void commit() = 0;                    // 将 next->current 生效（子类覆盖）
    virtual void reset() {}                     // 复位
    virtual void build() = 0;

    virtual std::string dev_name() const { return m_name; }

private:
    const std::string m_name;
};

class IPredictor : public DevIntf {
public:
    IPredictor() : DevIntf() {}
    explicit IPredictor(const std::string& n) : DevIntf(n) {}
    virtual ~IPredictor() = default;
};

class ICommit  : public DevIntf {
public:
    ICommit() : DevIntf() {}
    explicit ICommit(const std::string& n) : DevIntf(n) {}
    virtual ~ICommit() = default;
};

class IDecode : public DevIntf {
public:
    IDecode() : DevIntf() {}
    virtual ~IDecode() = default;
};

class IDispatch : public DevIntf {
public:
    IDispatch() : DevIntf() {}
    virtual ~IDispatch() = default;
};

class IExecute : public DevIntf {
public:
    IExecute() : DevIntf() {}
    explicit IExecute(const std::string& n) : DevIntf(n) {}
    virtual ~IExecute() = default;
};


class IFetch : public DevIntf {
public:
    IFetch() : DevIntf() {}
    virtual ~IFetch() = default;
};

class IIssue : public DevIntf {
public:
    IIssue() : DevIntf() {}
    explicit IIssue(const std::string& n) : DevIntf(n) {}
    virtual ~IIssue() = default;
};

class IRename : public DevIntf {
public:
    IRename() : DevIntf() {}
    explicit IRename(const std::string& n) : DevIntf(n) {}
    virtual ~IRename() = default;
};

class IRob : public DevIntf {
public:
    IRob() : DevIntf() {}
    explicit IRob(const std::string& n) : DevIntf(n) {}
    virtual ~IRob() = default;
};


class ICore : public DevIntf {
public:
    ICore() : DevIntf(), m_exit(false){}
    explicit ICore(const std::string& n) : DevIntf(n), m_exit(false) {}
    virtual ~ICore() {
        for (auto &m : m_modules) {
            delete_ptr(m);
        }
    };

public:
    void tick() {
        for(auto m : m_modules) {
            m->tick();
        }
    }

    void commit() {
        for (auto m : m_modules) {
            m->commit();
        }
    }
    void reset() {
        for (auto m : m_modules) {
            m->reset();
        }
    };

    virtual bool exit() {return m_exit;}

protected:
    void add_module(DevIntf *module) {
        m_modules.emplace_back(module);
    }

private:
    bool m_exit;

protected:
    std::vector<DevIntf*> m_modules;
};


// 2-bit 饱和计数器
struct Sat2 {
    uint8_t m_val = 2; // 取值0-3, >=2表示预测taken
    Sat2() = default;
    Sat2(int v): m_val((uint8_t)std::min(3, std::max(0, v))) {}
    bool taken() const { return m_val >= 2; }
    void update(bool taken) {
        if (taken) { if (m_val < 3) ++m_val; }
        else       { if (m_val > 0) --m_val; }
    }
};

// 可扩展 N 位饱和计数器
struct SatN {
    unsigned bits;
    unsigned val;
    unsigned max;
    SatN(unsigned bits = 2) : bits(bits), val(0) {
        max = (1u << bits) - 1;
    }
    bool predict_high() const { return val > (max / 2); }
    void inc() { if (val < max) ++val; }
    void dec() { if (val > 0) --val; }
};

#endif
