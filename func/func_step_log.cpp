#include "func/func_step_log.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

constexpr int k_seq_width = 4;
constexpr int k_pc_hex_width = 6;
constexpr int k_val_hex_width = 16;
constexpr int k_summary_width = 20;
constexpr int k_rs1_col_width = 26;
constexpr int k_rs2_col_width = 26;
constexpr int k_imm_col_width = 14;

const char* gpr(u32 idx)
{
    if (idx < 32) {
        return gpr_name[idx].c_str();
    }
    return "?";
}

std::string reg_operand(u32 idx, u64 val)
{
    std::ostringstream out;
    out << gpr(idx) << "(0x" << std::hex << val << std::dec << ')';
    return out.str();
}

std::string imm_operand(i64 val)
{
    std::ostringstream out;
    out << "imm=" << val;
    return out.str();
}

std::string rd_write_field(u32 idx, u64 val)
{
    std::ostringstream out;
    out << gpr(idx) << ": (0x" << std::hex << std::setfill('0') << std::setw(k_val_hex_width) << val
        << std::dec << std::setfill(' ') << ')';
    return out.str();
}

void append_padded(std::ostream& out, const std::string& field, int width)
{
    out << std::left << std::setw(width) << field << std::right;
}

void append_hex_field(std::ostream& out, const char* key, u64 val, int width)
{
    out << ' ' << key << ":0x" << std::hex << std::setfill('0') << std::setw(width) << val
        << std::dec << std::setfill(' ');
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

    out << std::setw(k_seq_width) << std::setfill('0') << seq << ":" << std::setfill(' ');
    append_hex_field(out, "pc", pc, k_pc_hex_width);

    const bool show_imm = imm != 0 || type == InstType::INT || type == InstType::LOAD
        || type == InstType::STORE || type == InstType::BRANCH;

    std::ostringstream summary;
    summary << "  " << name;
    if (rs1_v) {
        summary << " ," << gpr(rs1);
    }
    if (show_imm) {
        summary << ", " << imm;
    }
    append_padded(out, summary.str(), k_summary_width);

    std::ostringstream rd_text;
    switch (this->effect) {
    case RefEffect::GPR_WRITE:
        rd_text << rd_write_field(rd, rd_val);
        break;
    case RefEffect::LOAD:
        if (rd != 0) {
            rd_text << rd_write_field(rd, rd_val);
        } else {
            rd_text << "ld: (0x" << std::hex << std::setfill('0') << std::setw(k_val_hex_width) << rd_val
                    << std::dec << std::setfill(' ') << ')';
        }
        break;
    case RefEffect::STORE:
        rd_text << "st: (0x" << std::hex << std::setfill('0') << std::setw(k_val_hex_width) << mem_data
                << std::dec << std::setfill(' ') << ')';
        break;
    case RefEffect::BRANCH:
        if (rd != 0) {
            rd_text << rd_write_field(rd, rd_val);
        }
        if (branch_taken) {
            if (!rd_text.str().empty()) {
                rd_text << ' ';
            }
            rd_text << "tgt:0x" << std::hex << std::setfill('0') << std::setw(k_pc_hex_width)
                    << branch_target << std::dec << std::setfill(' ');
        } else {
            rd_text << "br:not_taken";
        }
        break;
    case RefEffect::HALT:
        rd_text << "halt";
        break;
    case RefEffect::FENCE:
        rd_text << "fence";
        break;
    case RefEffect::EXCEPTION:
        rd_text << "exc:" << exc_cause << " tval:0x" << std::hex << exc_tval << std::dec;
        break;
    case RefEffect::NONE:
    case RefEffect::SKIP:
        break;
    }

    if (fmt == FuncTraceFmt::STANDARD || fmt == FuncTraceFmt::VERBOSE) {
        out << " |";
        append_padded(out, rs1_v ? ("rs1:" + reg_operand(rs1, rs1_val)) : "rs1:", k_rs1_col_width);
        out << "| ";
        append_padded(out, rs2_v ? ("rs2:" + reg_operand(rs2, rs2_val)) : "rs2:", k_rs2_col_width);
        out << "| ";
        append_padded(out, show_imm ? imm_operand(imm) : "", k_imm_col_width);
        out << "| " << rd_text.str();
    }

    if (fmt == FuncTraceFmt::VERBOSE) {
        out << " | op=" << static_cast<int>(opcode)
            << " type=" << static_cast<int>(type)
            << " effect=" << static_cast<int>(this->effect);
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
