#ifndef RV_TCORE_REF_INST_LOG_H
#define RV_TCORE_REF_INST_LOG_H

#include <deque>
#include <optional>
#include <string>

#include "verify/ref_inst_record.h"

enum class VeriResTyp {
    OK,
    SEQ_MISMATCH,
    PC_MISMATCH,
    OPCODE_MISMATCH,
    EFFECT_MISMATCH,
    GPR_MISMATCH,
    MEM_MISMATCH,
    CTRL_MISMATCH,
    EXC_MISMATCH,
    REF_HALTED,
    REF_STEP_FAILED,
    SKIPPED,
};

class RefInstLog {
public:
    void                                        clear();

    void                                        append(const RefInstRecord& rec);
    bool                                        empty() const;
    u64                                         size() const;
    u64                                         cursor() const { return m_cursor; }
    bool                                        done() const { return m_cursor >= m_records.size(); }

    const RefInstRecord&                        expect() const;
    void                                        advance();

    VeriResTyp                                compare(const RefInstRecord& ref,
                                                      const RefInstRecord& dut) const;
    void                                        record_mismatch(const RefInstRecord& ref,
                                                                const RefInstRecord& dut,
                                                                VeriResTyp r);
    RefInstRecord                               recording(const MicroOp& inst,
                                                                const RetireMemInfo* mem) const;

    u64                                         mismatch_count() const { return m_mismatch_count; }
    const RefInstRecord*                        last_ref_mismatch() const;
    const RefInstRecord*                        last_dut_mismatch() const;
    VeriResTyp                                  last_result() const { return m_last_result; }
    std::string                                 result_string(VeriResTyp r) const;

private:
    std::deque<RefInstRecord>                   m_records;
    u64                                         m_cursor { 0 };
    u64                                         m_mismatch_count { 0 };
    VeriResTyp                                m_last_result { VeriResTyp::OK };
    std::optional<RefInstRecord>                m_last_ref_mismatch;
    std::optional<RefInstRecord>                m_last_dut_mismatch;
};

#endif
