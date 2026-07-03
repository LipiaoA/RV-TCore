#include "func/func_model.h"

#include <iostream>

#include "elf/elf_l.h"
#include "isa/decode.h"
#include "isa/inst_exec.h"
#include "util.h"

void FuncModel::setup_mem()
{
    m_mem.add_segment(STACK_POINTER - STACK_SIZE, STACK_SIZE);
    m_mem.store64(STACK_POINTER - 8, 0x1);
}

bool FuncModel::init(const std::string& path)
{
    m_mem = Memory{};
    m_arch = ArchState{};
    setup_mem();
    MappedFile elf;
    if (!elf.map(path.c_str())) {
        std::cerr << "failed to map elf: " << path << std::endl;
        return false;
    }
    ELFReader reader(elf.data, elf.size);
    if (!reader.read(m_mem)) {
        std::cerr << "failed to load elf" << std::endl;
        return false;
    }
    m_arch.pc = reader.get_entry();
    m_arch.halt = false;
    m_inst_cnt = 0;
    m_next_pc = m_arch.pc;
    m_arch.gpr[2] = STACK_POINTER;
    return true;
}

void FuncModel::reset()
{
    m_arch = ArchState{};
    m_inst_cnt = 0;
    m_next_pc = 0;
}

void FuncModel::trace_on(const std::string& path)
{
    m_trace = FuncTrace::inst().open(path);
}

void FuncModel::trace_off()
{
    FuncTrace::inst().close();
    m_trace = false;
}

void FuncModel::run(u64 max_inst)
{
    while (m_inst_cnt < max_inst) {
        if (m_arch.halt) {
            break;
        }
        if (!step()) {
            break;
        }
    }
}

bool FuncModel::fetch_dec(MicroOp& uop)
{
    u32 raw = 0;
    try {
        raw = m_mem.load32(m_arch.pc);
    } catch (const std::exception& e) {
        std::cerr << "fetch OOB at pc=0x" << std::hex << m_arch.pc << std::dec
                  << ": " << e.what() << std::endl;
        m_arch.halt = true;
        return false;
    }
    uop.pc = m_arch.pc;
    if (!isa::decode(raw, &uop)) {
        std::cerr << "illegal insn at pc=0x" << std::hex << m_arch.pc << std::dec << std::endl;
        m_arch.halt = true;
        return false;
    }
    if (uop.format != RV_FMT_NONE) {
        uop.imm = isa::extract_imm(uop.raw_inst, uop.format, uop.opcode);
    }
    uop.valid = true;
    if (uop.rs1.valid) {
        uop.rs1.val = m_arch.gpr[uop.rs1.atag];
        uop.rs1.ready = true;
    }
    if (uop.rs2.valid) {
        uop.rs2.val = m_arch.gpr[uop.rs2.atag];
        uop.rs2.ready = true;
    }
    return true;
}

bool FuncModel::exec_uop(MicroOp& uop, StepSnapshot& snap)
{
    snap = StepSnapshot {};

    switch (uop.opcode) {
    case OP_ECALL:
    case OP_EBREAK:
        snap.effect = RefEffect::HALT;
        m_arch.halt = true;
        return true;
    case OP_FENCE:
    case OP_FENCE_I:
    case OP_PAUSE:
        snap.effect = RefEffect::FENCE;
        return true;
    default:
        break;
    }

    auto write_rd = [&](u64 v) {
        if (uop.rd.valid && uop.rd.atag != 0) {
            m_arch.gpr[uop.rd.atag] = v;
            snap.rd = static_cast<u32>(uop.rd.atag);
            snap.rd_val = v;
        }
    };

    switch (uop.type) {
    case InstType::INT: {
        auto res = InstExec::exec_alu(&uop);
        if (!res.valid) {
            std::cerr << "unsupported int insn at pc=0x" << std::hex << uop.pc << std::dec << std::endl;
            m_arch.halt = true;
            return false;
        }
        if (uop.rd.valid && uop.rd.atag != 0) {
            snap.effect = RefEffect::GPR_WRITE;
            write_rd(res.value);
        } else {
            snap.effect = RefEffect::NONE;
        }
        break;
    }
    case InstType::BRANCH: {
        auto res = InstExec::exec_bru(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        snap.effect = RefEffect::BRANCH;
        snap.branch_taken = res.taken;
        snap.branch_target = res.target;
        if (res.taken) {
            m_next_pc = res.target;
        }
        if (res.write_rd && uop.rd.atag != 0) {
            m_arch.gpr[uop.rd.atag] = res.link;
            snap.rd = static_cast<u32>(uop.rd.atag);
            snap.rd_val = res.link;
        }
        break;
    }
    case InstType::LOAD: {
        auto res = InstExec::exec_agu(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        u64 val = 0;
        try {
            val = m_mem.load(res.addr, uop.ls_width);
        } catch (const std::exception& e) {
            std::cerr << "load OOB at pc=0x" << std::hex << uop.pc
                      << " addr=0x" << res.addr << std::dec
                      << ": " << e.what() << std::endl;
            m_arch.halt = true;
            return false;
        }
        switch (uop.opcode) {
        case OP_LB: val = static_cast<u64>(sextend64(val & 0xFF, 8)); break;
        case OP_LH: val = static_cast<u64>(sextend64(val & 0xFFFF, 16)); break;
        case OP_LW: val = static_cast<u64>(sextend64(val & 0xFFFFFFFFu, 32)); break;
        default: break;
        }
        snap.effect = RefEffect::LOAD;
        snap.mem_addr = res.addr;
        snap.mem_width = uop.ls_width;
        if (uop.rd.valid && uop.rd.atag != 0) {
            snap.rd = static_cast<u32>(uop.rd.atag);
        }
        write_rd(val);
        break;
    }
    case InstType::STORE: {
        auto res = InstExec::exec_agu(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        try {
            m_mem.store(res.addr, uop.rs2.val, uop.ls_width);
        } catch (const std::exception& e) {
            std::cerr << "store OOB at pc=0x" << std::hex << uop.pc
                      << " addr=0x" << res.addr << std::dec
                      << ": " << e.what() << std::endl;
            m_arch.halt = true;
            return false;
        }
        snap.effect = RefEffect::STORE;
        snap.mem_addr = res.addr;
        snap.mem_data = uop.rs2.val;
        snap.mem_width = uop.ls_width;
        break;
    }
    default:
        std::cerr << "unsupported insn type at pc=0x" << std::hex << uop.pc << std::dec << std::endl;
        m_arch.halt = true;
        return false;
    }
    return true;
}

bool FuncModel::step(RefInstRecord* record_out)
{
    if (m_arch.halt) {
        return false;
    }
    m_next_pc = m_arch.pc + 4;

    MicroOp uop {};
    if (!fetch_dec(uop)) {
        return false;
    }

    StepSnapshot snap {};
    if (!exec_uop(uop, snap)) {
        return false;
    }

    snap.next_pc = m_next_pc;
    m_arch.pc = m_next_pc;
    if (record_out) {
        *record_out = ref_record(m_inst_cnt, uop, snap);
    }
    if (m_trace) {
        FuncTrace::inst().write(FuncStepLog::from(m_inst_cnt, uop, snap));
    }
    m_inst_cnt++;
    return true;
}
