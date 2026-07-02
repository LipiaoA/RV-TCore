#ifndef RV_TCORE_MODEL_H
#define RV_TCORE_MODEL_H

#include "intf.h"
#include "mem.h"
#include "elf_l.h"
#include "core.h"
#include "top.h"
#include "peripheral.h"
#include "func/func_model.h"

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
    const PerfStats&                    perf_stats() const;

private:
    u64                                 m_entry;

private:
    Memory                              *m_mem;
    Core                                *m_core;
    LogSystem                           *m_log;
    Top                                 *m_top;
    FuncModel                           *m_ref { nullptr };
};

#endif
