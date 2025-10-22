#ifndef RV_TCORE_MODEL_H
#define RV_TCORE_MODEL_H

#include "intf.h"
#include "memory.h"
#include "elf_l.h"
#include "core.h"
#include "peripheral.h"

class Model {
public:
    Model(const std::string& name) : DevIntf(name) {}
    Model() = delete;
    ~Model();

public:
    bool init(const std::string& path);
    bool build();
    void reset();
    void run();

private:
    Memory *m_mem;
    ELFReader m_elf;

private:
    ICore *m_core;
};

#endif