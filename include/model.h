#ifndef RV_TCORE_MODEL_H
#define RV_TCORE_MODEL_H

#include "intf.h"
#include "mem.h"
#include "elf_l.h"
#include "core.h"
#include "top.h"
#include "peripheral.h"

class Model {
public:
    Model(const std::string& name) {}
    Model() = delete;
    ~Model();

public:
    bool                                init(const std::string& path);
    bool                                build();
    void                                reset();
    void                                run();

private:
    u64                                 m_entry;

private:
    Memory                              *m_mem;
    ICore                               *m_core;
    LogSystem                           *m_log;
};

#endif
