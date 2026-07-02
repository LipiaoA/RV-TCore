#include "verify/ref_model_checker.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "log/log.h"

VeriRef& VeriRef::instance()
{
    static VeriRef s_checker;
    return s_checker;
}

void VeriRef::reset()
{
    m_log.clear();
    m_ref_halted = false;
    m_checked_count = 0;
    m_last_error.clear();
}

void VeriRef::bind(FuncModel* ref)
{
    m_ref = ref;
}

bool VeriRef::passed() const
{
    return mismatch_count() == 0;
}

u64 VeriRef::mismatch_count() const
{
    return m_log.mismatch_count();
}

static bool is_skippable_type(InstType type)
{
    switch (type) {
    case InstType::FP:
    case InstType::VECTOR:
    case InstType::TENSOR:
        return true;
    default:
        return false;
    }
}

void VeriRef::report_mismatch(const RefInstRecord& ref,
                                      const RefInstRecord& dut,
                                      VeriResTyp r)
{
    std::ostringstream oss;
    oss << m_log.result_string(r)
        << " seq=" << ref.seq
        << " ref_pc=0x" << std::hex << ref.pc
        << " dut_pc=0x" << dut.pc
        << " ref_effect=" << static_cast<int>(ref.effect)
        << " dut_effect=" << static_cast<int>(dut.effect)
        << std::dec;
    m_last_error = oss.str();
    m_log.record_mismatch(ref, dut, r);

    LOG_LEVEL1("VeriRef", "%s", m_last_error.c_str());
    std::cerr << "[VeriRef] " << m_last_error << std::endl;
    dump_mismatch();
}

bool VeriRef::check_retire(const MicroOp& inst, const RetireMemInfo* mem)
{
    if (!m_enabled || !m_ref) {
        return true;
    }

    if (is_skippable_type(inst.type)) {
        ++m_checked_count;
        return true;
    }

    if (inst.type == InstType::STD) {
        return true;
    }

    if (m_ref_halted) {
        RefInstRecord dut = m_log.recording(inst, mem);
        RefInstRecord ref_rec {};
        ref_rec.seq = dut.seq;
        ref_rec.effect = RefEffect::HALT;
        report_mismatch(ref_rec, dut, VeriResTyp::REF_HALTED);
        return false;
    }

    RefInstRecord ref {};
    if (!m_ref->step(&ref)) {
        m_ref_halted = true;
        RefInstRecord dut = m_log.recording(inst, mem);
        dut.seq = ref.seq;
        report_mismatch(ref, dut, VeriResTyp::REF_STEP_FAILED);
        return false;
    }

    RefInstRecord dut = m_log.recording(inst, mem);
    dut.seq = ref.seq;

    VeriResTyp result = m_log.compare(ref, dut);
    if (result == VeriResTyp::SKIPPED) {
        ++m_checked_count;
        return true;
    }

    if (result != VeriResTyp::OK) {
        report_mismatch(ref, dut, result);
        return false;
    }

    m_log.append(ref);
    m_log.advance();
    ++m_checked_count;
    return true;
}

void VeriRef::dump_mismatch() const
{
    const RefInstRecord* ref = m_log.last_ref_mismatch();
    const RefInstRecord* dut = m_log.last_dut_mismatch();
    if (!ref || !dut) {
        return;
    }

    std::cerr << "  REF: pc=0x" << std::hex << ref->pc
              << " op=" << static_cast<int>(ref->opcode)
              << " effect=" << static_cast<int>(ref->effect)
              << " rd=x" << std::dec << ref->rd
              << " val=0x" << std::hex << ref->rd_val
              << " mem_addr=0x" << ref->mem_addr
              << " mem_data=0x" << ref->mem_data
              << " next_pc=0x" << ref->next_pc
              << std::dec << std::endl;
    std::cerr << "  DUT: pc=0x" << std::hex << dut->pc
              << " op=" << static_cast<int>(dut->opcode)
              << " effect=" << static_cast<int>(dut->effect)
              << " rd=x" << std::dec << dut->rd
              << " val=0x" << std::hex << dut->rd_val
              << " mem_addr=0x" << dut->mem_addr
              << " mem_data=0x" << dut->mem_data
              << " next_pc=0x" << dut->next_pc
              << std::dec << std::endl;
}
