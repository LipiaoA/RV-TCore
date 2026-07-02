#include "func/func_step_log.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

const char* gpr(u32 idx)
{
    if (idx < 32) {
        return gpr_name[idx].c_str();
    }
    return "?";
}

void hex_field(std::ostream& out, const char* key, u64 val, bool nl = false)
{
    out << key << "=0x" << std::hex << val << std::dec;
    if (nl) {
        out << '\n';
    }
}

}

FuncStepLog FuncStepLog::from(u64 seq, const MicroOp& uop, const StepSnapshot& snap)
{
    FuncStepLog log {};
    log.seq = seq;
    log.pc = uop.pc;
    log.raw = static_cast<u32>(uop.raw_inst);
    log.opcode = uop.opcode;
    log.type = uop.type;
    log.name = uop.inst_name;

    if (uop.rs1.valid) {
        log.rs1_v = true;
        log.rs1 = static_cast<u32>(uop.rs1.atag);
        log.rs1_val = uop.rs1.val;
    }
    if (uop.rs2.valid) {
        log.rs2_v = true;
        log.rs2 = static_cast<u32>(uop.rs2.atag);
        log.rs2_val = uop.rs2.val;
    }
    log.imm = uop.imm;

    log.effect = snap.effect;
    log.rd = snap.rd;
    log.rd_val = snap.rd_val;
    log.mem_addr = snap.mem_addr;
    log.mem_data = snap.mem_data;
    log.mem_width = snap.mem_width;
    log.branch_taken = snap.branch_taken;
    log.branch_target = snap.branch_target;
    log.next_pc = snap.next_pc;
    log.exc_cause = snap.exc_cause;
    log.exc_tval = snap.exc_tval;
    return log;
}

void FuncStepLog::format(std::ostream& out, FuncTraceFmt fmt) const
{
    if (fmt == FuncTraceFmt::COMPACT) {
        out << "pc=0x" << std::hex << pc << std::dec << ' ' << name;
        if (effect == RefEffect::GPR_WRITE || effect == RefEffect::LOAD) {
            out << ' ' << gpr(rd) << "<=0x" << std::hex << rd_val << std::dec;
        } else if (effect == RefEffect::BRANCH && rd != 0) {
            out << ' ' << gpr(rd) << "<=0x" << std::hex << rd_val << std::dec;
        }
        out << '\n';
        return;
    }

    out << '#' << std::setw(5) << std::setfill('0') << seq << std::setfill(' ');
    hex_field(out, " pc", pc);
    out << " raw=0x" << std::hex << std::setw(8) << std::setfill('0') << raw
        << std::setfill(' ') << std::dec << ' ' << name;

    if (fmt == FuncTraceFmt::STANDARD || fmt == FuncTraceFmt::VERBOSE) {
        out << " |";
        if (rs1_v) {
            out << ' ' << gpr(rs1) << "(0x" << std::hex << rs1_val << std::dec << ')';
        }
        if (rs2_v) {
            out << ' ' << gpr(rs2) << "(0x" << std::hex << rs2_val << std::dec << ')';
        }
        if (imm != 0 || type == InstType::INT || type == InstType::LOAD
            || type == InstType::STORE || type == InstType::BRANCH) {
            out << " imm=" << imm;
        }
        out << " |";
    }

    switch (effect) {
    case RefEffect::GPR_WRITE:
        out << " wr " << gpr(rd) << "<=0x" << std::hex << rd_val << std::dec;
        break;
    case RefEffect::LOAD:
        out << " ld [0x" << std::hex << mem_addr << ',' << std::dec << mem_width
            << "]=>0x" << std::hex << rd_val << std::dec;
        if (rd != 0) {
            out << " wr " << gpr(rd) << "<=0x" << std::hex << rd_val << std::dec;
        }
        break;
    case RefEffect::STORE:
        out << " st [0x" << std::hex << mem_addr << ',' << std::dec << mem_width
            << "]<=0x" << std::hex << mem_data << std::dec;
        break;
    case RefEffect::BRANCH:
        out << " br " << (branch_taken ? "taken" : "not_taken");
        if (branch_taken) {
            out << " tgt=0x" << std::hex << branch_target << std::dec;
        }
        if (rd != 0) {
            out << " wr " << gpr(rd) << "<=0x" << std::hex << rd_val << std::dec;
        }
        break;
    case RefEffect::HALT:
        out << " halt";
        break;
    case RefEffect::FENCE:
        out << " fence";
        break;
    case RefEffect::EXCEPTION:
        out << " exc cause=" << exc_cause << " tval=0x" << std::hex << exc_tval << std::dec;
        break;
    case RefEffect::NONE:
    case RefEffect::SKIP:
        break;
    }

    if (fmt == FuncTraceFmt::STANDARD || fmt == FuncTraceFmt::VERBOSE) {
        out << " | next=0x" << std::hex << next_pc << std::dec;
    }

    if (fmt == FuncTraceFmt::VERBOSE) {
        out << " | op=" << static_cast<int>(opcode)
            << " type=" << static_cast<int>(type)
            << " effect=" << static_cast<int>(effect);
    }

    out << '\n';
}

FuncTrace& FuncTrace::inst()
{
    static FuncTrace s_trace;
    return s_trace;
}

bool FuncTrace::open(const std::string& path)
{
    close();
    m_out.open(path, std::ios::out | std::ios::trunc);
    m_on = m_out.is_open();
    return m_on;
}

void FuncTrace::close()
{
    if (m_out.is_open()) {
        m_out.flush();
        m_out.close();
    }
    m_on = false;
}

void FuncTrace::write(const FuncStepLog& log)
{
    if (!m_on) {
        return;
    }

    std::ostringstream line;
    log.format(line, m_fmt);
    const std::string text = line.str();

    if (m_out.is_open()) {
        m_out << text;
    }
    if (m_console) {
        std::cout << text;
    }
}

void FuncTrace::flush()
{
    if (m_out.is_open()) {
        m_out.flush();
    }
    if (m_console) {
        std::cout.flush();
    }
}
