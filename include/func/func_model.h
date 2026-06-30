#ifndef RV_TCORE_FUNC_MODEL_H
#define RV_TCORE_FUNC_MODEL_H

#include <cstdint>
#include <string>

#include "arch_state.h"
#include "decode/inst.h"
#include "mem.h"

class FuncModel {
public:
    FuncModel() = default;

    bool                                            init(const std::string& path);
    void                                            reset();
    void                                            run(u64 max_inst = UINT64_MAX);
    bool                                            step();

    const ArchState&                                arch() const { return m_arch; }
    u64                                             inst_cnt() const { return m_inst_cnt; }

private:
    bool                                            fetch_dec(MicroOp& uop);
    bool                                            exec_uop(MicroOp& uop);
    void                                            setup_mem();

private:
    Memory                                          m_mem;
    ArchState                                       m_arch;
    u64                                             m_inst_cnt { 0 };
};

#endif
