#ifndef RV_TCORE_REF_MODEL_CHECKER_H
#define RV_TCORE_REF_MODEL_CHECKER_H

#include <string>

#include "func/func_model.h"
#include "verify/ref_inst_log.h"

class VeriRef {
public:
    static VeriRef&                     instance();

    void                                        reset();
    void                                        enable(bool on) { m_enabled = on; }
    bool                                        enabled() const { return m_enabled; }

    void                                        bind(FuncModel* ref);

    bool                                        check_retire(const MicroOp& inst,
                                                             const RetireMemInfo* mem = nullptr);
    bool                                        passed() const;
    u64                                         mismatch_count() const;
    u64                                         checked_count() const { return m_checked_count; }
    VeriResTyp                                last_result() const { return m_log.last_result(); }
    std::string                                 last_error() const { return m_last_error; }

    void                                        dump_mismatch() const;

private:
    VeriRef() = default;

    void                                        report_mismatch(const RefInstRecord& ref,
                                                                const RefInstRecord& dut,
                                                                VeriResTyp r);

    FuncModel*                                  m_ref { nullptr };
    RefInstLog                                  m_log;
    bool                                        m_enabled { false };
    bool                                        m_ref_halted { false };
    u64                                         m_checked_count { 0 };
    std::string                                 m_last_error;
};

#endif
