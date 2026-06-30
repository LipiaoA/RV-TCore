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
    m_arch.gpr[2] = STACK_POINTER;
    return true;
}

void FuncModel::reset()
{
    m_arch = ArchState{};
    m_arch.halt = false;
}

void FuncModel::run(u64 max_inst)
{
    while (m_inst_cnt < max_inst && step()) {
    }
}

bool FuncModel::fetch_dec(MicroOp& uop)
{
    u32 raw = m_mem.load32(m_arch.pc);
    uop.pc = m_arch.pc;
    if (!isa::decode(raw, &uop)) {
        std::cerr << "illegal insn at pc=0x" << std::hex << m_arch.pc << std::dec << std::endl;
        m_arch.halt = true;
        return false;
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

bool FuncModel::exec_uop(MicroOp& uop)
{
    switch (uop.opcode) {
    case OP_ECALL:
    case OP_EBREAK:
        m_arch.halt = true;
        return true;
    case OP_FENCE:
    case OP_FENCE_I:
        return true;
    default:
        break;
    }

    switch (uop.type) {
    case InstType::INT: {
        auto res = InstExec::exec_alu(&uop);
        if (!res.valid) {
            std::cerr << "unsupported int insn at pc=0x" << std::hex << uop.pc << std::dec << std::endl;
            m_arch.halt = true;
            return false;
        }
        if (uop.rd.valid) {
            m_arch.gpr[uop.rd.atag] = res.value;
        }
        break;
    }
    case InstType::BRANCH: {
        auto res = InstExec::exec_bru(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        if (res.taken) {
            m_arch.pc = res.target;
        }
        if (res.write_rd) {
            m_arch.gpr[uop.rd.atag] = res.link;
        }
        break;
    }
    case InstType::LOAD: {
        auto res = InstExec::exec_agu(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        u64 val = m_mem.load(res.addr, uop.ls_width);
        if (uop.rd.valid) {
            m_arch.gpr[uop.rd.atag] = val;
        }
        break;
    }
    case InstType::STORE: {
        auto res = InstExec::exec_agu(&uop);
        if (!res.valid) {
            m_arch.halt = true;
            return false;
        }
        m_mem.store(res.addr, uop.rs2.val, uop.ls_width);
        break;
    }
    default:
        std::cerr << "unsupported insn type at pc=0x" << std::hex << uop.pc << std::dec << std::endl;
        m_arch.halt = true;
        return false;
    }
    return true;
}

bool FuncModel::step()
{
    if (m_arch.halt) {
        return false;
    }

    MicroOp uop {};
    if (!fetch_dec(uop)) {
        return false;
    }

    if (!exec_uop(uop)) {
        return false;
    }

    m_arch.pc += 4;
    m_inst_cnt++;
    return !m_arch.halt;
}
