#ifndef RV_TCORE_FUNC_STEP_LOG_H
#define RV_TCORE_FUNC_STEP_LOG_H

#include <fstream>
#include <string>

#include "decode/inst.h"
#include "verify/ref_inst_record.h"

enum class FuncTraceFmt {
    COMPACT,
    STANDARD,
    VERBOSE,
};

class FuncStepLog {
public:
    static FuncStepLog                            from(u64 seq, const MicroOp& uop, const StepSnapshot& snap);

    void                                          format(std::ostream& out, FuncTraceFmt fmt) const;

    u64                                           seq { 0 };
    pc_t                                          pc { 0 };
    u32                                           raw { 0 };
    InstOp                                        opcode { static_cast<InstOp>(0) };
    InstType                                      type { InstType::NONE };
    std::string                                   name;

    bool                                          rs1_v { false };
    u32                                           rs1 { 0 };
    u64                                           rs1_val { 0 };
    bool                                          rs2_v { false };
    u32                                           rs2 { 0 };
    u64                                           rs2_val { 0 };
    i64                                           imm { 0 };

    RefEffect                                     effect { RefEffect::NONE };
    u32                                           rd { 0 };
    u64                                           rd_val { 0 };
    u64                                           mem_addr { 0 };
    u64                                           mem_data { 0 };
    u32                                           mem_width { 0 };
    bool                                          branch_taken { false };
    pc_t                                          branch_target { 0 };
    pc_t                                          next_pc { 0 };
    u64                                           exc_cause { 0 };
    pc_t                                          exc_tval { 0 };
};

class FuncTrace {
public:
    static FuncTrace&                             inst();

    bool                                          open(const std::string& path = "func_inst_trace.log");
    void                                          close();
    bool                                          on() const { return m_on; }

    void                                          set_fmt(FuncTraceFmt fmt) { m_fmt = fmt; }
    void                                          set_console(bool enable) { m_console = enable; }

    void                                          write(const FuncStepLog& log);
    void                                          flush();

private:
    FuncTrace() = default;

    std::ofstream                                 m_out;
    bool                                          m_on { false };
    FuncTraceFmt                                  m_fmt { FuncTraceFmt::STANDARD };
    bool                                          m_console { false };
};

#endif
