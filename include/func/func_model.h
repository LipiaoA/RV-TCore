#ifndef RV_TCORE_FUNC_MODEL_H
#define RV_TCORE_FUNC_MODEL_H

#include <cstdint>
#include <string>

#include "arch_state.h"
#include "decode/inst.h"
#include "func/func_step_log.h"
#include "mem.h"
#include "verify/ref_inst_record.h"

class FuncModel {
public:
    FuncModel() = default;

    bool                                            init(const std::string& path);
    void                                            reset();
    void                                            run(u64 max_inst = UINT64_MAX);
    bool                                            step(RefInstRecord* record_out = nullptr);

    void                                            trace_on(const std::string& path = "func_inst_trace.log");
    void                                            trace_off();

    const ArchState&                                arch() const { return m_arch; }
    u64                                             inst_cnt() const { return m_inst_cnt; }

private:
    bool                                            fetch_dec(MicroOp& uop);
    bool                                            exec_uop(MicroOp& uop, StepSnapshot& snap);
    void                                            setup_mem();

private:
    Memory                                          m_mem;
    ArchState                                       m_arch;
    u64                                             m_inst_cnt { 0 };
    pc_t                                            m_next_pc;
    bool                                            m_trace { false };
};

#endif
