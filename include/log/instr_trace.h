#ifndef RV_TCORE_INSTR_TRACE_H
#define RV_TCORE_INSTR_TRACE_H

#include <cstdint>
#include <fstream>
#include <string>

#include "rv_def.h"

// Commit 阶段退休指令轨迹：每行记录 pc 与 result
class InstrTrace {
public:
    static InstrTrace& instance();

    // 打开输出文件；path 为空则使用默认路径 instr_trace.log
    bool                                        open(const std::string& path = "instr_trace.log");
    void                                        close();

    bool                                        enabled() const { return m_enabled; }

    void                                        record_retire(pc_t pc, u64 result);
    void                                        flush();

private:
    InstrTrace();
    ~InstrTrace();

    InstrTrace(const InstrTrace&) = delete;
    InstrTrace& operator=(const InstrTrace&) = delete;

    std::ofstream                               m_out;
    bool                                        m_enabled { false };
};

#endif
