#include "verify/ref_inst_log.h"

#include <cassert>
#include <sstream>

#include "isa/inst_exec.h"
#include "log/log.h"

void RefInstLog::clear()
{
    m_records.clear();
    m_cursor = 0;
    m_mismatch_count = 0;
    m_last_result = VeriResTyp::OK;
    m_last_ref_mismatch.reset();
    m_last_dut_mismatch.reset();
}

void RefInstLog::append(const RefInstRecord& rec)
{
    m_records.push_back(rec);
}

bool RefInstLog::empty() const
{
    return m_records.empty();
}

u64 RefInstLog::size() const
{
    return m_records.size();
}

const RefInstRecord& RefInstLog::expect() const
{
    assert(m_cursor < m_records.size());
    return m_records[m_cursor];
}

void RefInstLog::advance()
{
    if (m_cursor < m_records.size()) {
        ++m_cursor;
    }
}

const RefInstRecord* RefInstLog::last_ref_mismatch() const
{
    return m_last_ref_mismatch ? &*m_last_ref_mismatch : nullptr;
}

const RefInstRecord* RefInstLog::last_dut_mismatch() const
{
    return m_last_dut_mismatch ? &*m_last_dut_mismatch : nullptr;
}

std::string RefInstLog::result_string(VeriResTyp r) const
{
    switch (r) {
    case VeriResTyp::OK: return "OK";
    case VeriResTyp::SEQ_MISMATCH: return "SEQ_MISMATCH";
    case VeriResTyp::PC_MISMATCH: return "PC_MISMATCH";
    case VeriResTyp::OPCODE_MISMATCH: return "OPCODE_MISMATCH";
    case VeriResTyp::EFFECT_MISMATCH: return "EFFECT_MISMATCH";
    case VeriResTyp::GPR_MISMATCH: return "GPR_MISMATCH";
    case VeriResTyp::MEM_MISMATCH: return "MEM_MISMATCH";
    case VeriResTyp::CTRL_MISMATCH: return "CTRL_MISMATCH";
    case VeriResTyp::EXC_MISMATCH: return "EXC_MISMATCH";
    case VeriResTyp::REF_HALTED: return "REF_HALTED";
    case VeriResTyp::REF_STEP_FAILED: return "REF_STEP_FAILED";
    case VeriResTyp::SKIPPED: return "SKIPPED";
    default: return "UNKNOWN";
    }
}

void RefInstLog::record_mismatch(const RefInstRecord& ref, const RefInstRecord& dut, VeriResTyp r)
{
    ++m_mismatch_count;
    m_last_result = r;
    m_last_ref_mismatch = ref;
    m_last_dut_mismatch = dut;
}

VeriResTyp RefInstLog::compare(const RefInstRecord& ref, const RefInstRecord& dut) const
{
    if (ref.effect == RefEffect::SKIP || dut.effect == RefEffect::SKIP) {
        return VeriResTyp::SKIPPED;
    }

    if (ref.pc != dut.pc) {
        return VeriResTyp::PC_MISMATCH;
    }
    if (ref.opcode != dut.opcode) {
        return VeriResTyp::OPCODE_MISMATCH;
    }
    if (ref.effect != dut.effect) {
        return VeriResTyp::EFFECT_MISMATCH;
    }

    switch (ref.effect) {
    case RefEffect::GPR_WRITE:
        if (ref.rd != dut.rd || ref.rd_val != dut.rd_val) {
            return VeriResTyp::GPR_MISMATCH;
        }
        break;
    case RefEffect::LOAD:
        if (ref.rd != dut.rd || ref.rd_val != dut.rd_val
            || ref.mem_addr != dut.mem_addr || ref.mem_width != dut.mem_width) {
            return VeriResTyp::MEM_MISMATCH;
        }
        break;
    case RefEffect::STORE:
        if (ref.mem_addr != dut.mem_addr || ref.mem_data != dut.mem_data
            || ref.mem_width != dut.mem_width) {
            return VeriResTyp::MEM_MISMATCH;
        }
        break;
    case RefEffect::BRANCH:
        if (ref.branch_taken != dut.branch_taken
            || ref.branch_target != dut.branch_target
            || ref.next_pc != dut.next_pc) {
            return VeriResTyp::CTRL_MISMATCH;
        }
        if (ref.rd != 0 && (ref.rd != dut.rd || ref.rd_val != dut.rd_val)) {
            return VeriResTyp::GPR_MISMATCH;
        }
        break;
    case RefEffect::EXCEPTION:
        if (ref.exc_cause != dut.exc_cause || ref.exc_tval != dut.exc_tval) {
            return VeriResTyp::EXC_MISMATCH;
        }
        break;
    case RefEffect::HALT:
    case RefEffect::FENCE:
    case RefEffect::NONE:
    case RefEffect::SKIP:
        break;
    }

    if (ref.seq != dut.seq) {
        return VeriResTyp::SEQ_MISMATCH;
    }

    return VeriResTyp::OK;
}

RefInstRecord RefInstLog::recording(const MicroOp& inst, const RetireMemInfo* mem) const
{
    RefInstRecord rec {};
    rec.pc = inst.pc;
    rec.raw_inst = static_cast<u32>(inst.raw_inst);
    rec.opcode = inst.opcode;
    rec.type = inst.type;

    if (inst.fetch_fault || inst.has_exception) {
        rec.effect = RefEffect::EXCEPTION;
        rec.exc_cause = inst.exception_cause;
        rec.exc_tval = inst.exception_tval;
        return rec;
    }

    switch (inst.opcode) {
    case OP_ECALL:
    case OP_EBREAK:
        rec.effect = RefEffect::HALT;
        return rec;
    case OP_FENCE:
    case OP_FENCE_I:
    case OP_PAUSE:
        rec.effect = RefEffect::FENCE;
        return rec;
    default:
        break;
    }

    switch (inst.type) {
    case InstType::INT:
        if (inst.rd.valid && inst.rd.atag != 0) {
            rec.effect = RefEffect::GPR_WRITE;
            rec.rd = static_cast<u32>(inst.rd.atag);
            rec.rd_val = inst.rd.val;
        } else {
            rec.effect = RefEffect::NONE;
        }
        break;
    case InstType::FP:
        rec.effect = RefEffect::SKIP;
        break;
    case InstType::LOAD: {
        rec.effect = RefEffect::LOAD;
        rec.mem_width = inst.ls_width;
        if (inst.rd.valid && inst.rd.atag != 0) {
            rec.rd = static_cast<u32>(inst.rd.atag);
            rec.rd_val = inst.rd.val;
        }
        if (mem && mem->valid) {
            rec.mem_addr = mem->addr;
            if (mem->width != 0) {
                rec.mem_width = mem->width;
            }
        }
        if (rec.mem_addr == 0) {
            MicroOp tmp = inst;
            auto agu = InstExec::exec_agu(&tmp);
            if (agu.valid) {
                rec.mem_addr = agu.addr;
            }
        }
        break;
    }
    case InstType::STORE:
    case InstType::STA:
    case InstType::STD: {
        rec.effect = RefEffect::STORE;
        rec.mem_width = inst.ls_width;
        rec.mem_data = inst.rs2.val;
        if (mem && mem->valid) {
            rec.mem_addr = mem->addr;
            rec.mem_data = mem->data;
            if (mem->width != 0) {
                rec.mem_width = mem->width;
            }
        }
        if (rec.mem_addr == 0) {
            MicroOp tmp = inst;
            auto agu = InstExec::exec_agu(&tmp);
            if (agu.valid) {
                rec.mem_addr = agu.addr;
            }
        }
        break;
    }
    case InstType::BRANCH: {
        rec.effect = RefEffect::BRANCH;
        MicroOp tmp = inst;
        auto bru = InstExec::exec_bru(&tmp);
        rec.branch_taken = bru.valid && bru.taken;
        rec.branch_target = bru.target;
        rec.next_pc = bru.taken ? bru.target : (inst.pc + inst.inst_size);
        if (inst.rd.valid && inst.rd.atag != 0) {
            rec.rd = static_cast<u32>(inst.rd.atag);
            rec.rd_val = inst.rd.val;
        } else if (bru.write_rd) {
            rec.rd = static_cast<u32>(inst.rd.atag);
            rec.rd_val = bru.link;
        }
        break;
    }
    case InstType::VECTOR:
    case InstType::TENSOR:
        rec.effect = RefEffect::SKIP;
        break;
    default:
        rec.effect = RefEffect::NONE;
        break;
    }

    return rec;
}
