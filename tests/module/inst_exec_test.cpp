#include <cassert>
#include <cstdlib>

#include "inst_exec.h"
#include "isa/decode.h"

using namespace isa;

static MicroOp make_inst(InstOp op, u64 pc = 0x1000)
{
    MicroOp inst {};
    inst.valid = true;
    inst.opcode = op;
    inst.pc = pc;
    inst.rd.valid = true;
    return inst;
}

int main()
{
    assert(is_int(OP_LUI));
    assert(is_int(OP_AUIPC));
    assert(is_int(OP_ADDW));
    assert(is_int(OP_ADDD));
    assert(is_load_store(OP_LDU));
    assert(is_load_store(OP_SQ));

    {
        MicroOp inst = make_inst(OP_ADDI);
        inst.rs1.val = 1;
        inst.imm = -2;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.write_rd);
        assert(result.value == static_cast<u64>(-1));
    }

    {
        MicroOp inst = make_inst(OP_SRA);
        inst.rs1.val = 0x8000000000000000ull;
        inst.rs2.val = 1;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0xc000000000000000ull);
    }

    {
        MicroOp inst = make_inst(OP_ADDW);
        inst.rs1.val = 0x000000007fffffffull;
        inst.rs2.val = 1;
        inst.imm = 12345;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0xffffffff80000000ull);
    }

    {
        MicroOp inst = make_inst(OP_MUL);
        inst.rs1.val = 6;
        inst.rs2.val = 7;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 42);
    }

    {
        MicroOp inst = make_inst(OP_MULH);
        inst.rs1.val = 0xffffffffffffffffull; // -1
        inst.rs2.val = 0xffffffffffffffffull; // -1
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0); // (-1)*(-1) high = 0
    }

    {
        MicroOp inst = make_inst(OP_MULHU);
        inst.rs1.val = 0xffffffffffffffffull;
        inst.rs2.val = 0xffffffffffffffffull;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0xfffffffffffffffeull);
    }

    {
        MicroOp inst = make_inst(OP_MULHSU);
        inst.rs1.val = static_cast<u64>(-1); // signed -1
        inst.rs2.val = 2;                    // unsigned 2
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0xffffffffffffffffull); // high of -2
    }

    {
        MicroOp inst = make_inst(OP_DIV);
        inst.rs1.val = static_cast<u64>(-20);
        inst.rs2.val = 3;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == static_cast<u64>(-6));
    }

    {
        MicroOp inst = make_inst(OP_DIV);
        inst.rs1.val = 0x8000000000000000ull;
        inst.rs2.val = static_cast<u64>(-1);
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0x8000000000000000ull); // overflow → dividend
    }

    {
        MicroOp inst = make_inst(OP_DIVU);
        inst.rs1.val = 20;
        inst.rs2.val = 0;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == ~0ull);
    }

    {
        MicroOp inst = make_inst(OP_REM);
        inst.rs1.val = static_cast<u64>(-20);
        inst.rs2.val = 3;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == static_cast<u64>(-2));
    }

    {
        MicroOp inst = make_inst(OP_MULW);
        inst.rs1.val = 0x100000007ull;
        inst.rs2.val = 0x100000003ull;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 21); // 7*3, upper bits ignored
    }

    {
        MicroOp inst = make_inst(OP_DIVW);
        inst.rs1.val = 0xffffffff80000000ull; // INT32_MIN
        inst.rs2.val = static_cast<u64>(-1);
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 0xffffffff80000000ull);
    }

    {
        MicroOp inst = make_inst(OP_REMUW);
        inst.rs1.val = 0xffffffff00000007ull;
        inst.rs2.val = 3;
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(result.valid);
        assert(result.value == 1);
    }

    {
        MicroOp inst = make_inst(OP_MULD);
        InstExec::AluExecResult result = InstExec::exec_alu(&inst);
        assert(!result.valid); // RV128, unsupported on RV64
    }

    {
        MicroOp inst = make_inst(OP_JALR, 0x2000);
        inst.rs1.val = 0x3003;
        inst.imm = 4;
        InstExec::BruExecResult result = InstExec::exec_bru(&inst);
        assert(result.valid);
        assert(result.taken);
        assert(result.target == 0x3006);
        assert(result.write_rd);
        assert(result.link == 0x2004);
    }

    {
        MicroOp inst = make_inst(OP_BLT, 0x4000);
        inst.rd.valid = false;
        inst.rs1.val = static_cast<u64>(-1);
        inst.rs2.val = 1;
        inst.imm = 0x20;
        InstExec::BruExecResult result = InstExec::exec_bru(&inst);
        assert(result.valid);
        assert(result.taken);
        assert(result.target == 0x4020);
        assert(!result.write_rd);
    }

    {
        MicroOp inst = make_inst(OP_LD);
        inst.rs1.val = 0x80000000;
        inst.imm = -8;
        InstExec::AguExecResult result = InstExec::exec_agu(&inst);
        assert(result.valid);
        assert(result.addr == 0x7ffffff8);
    }

    {
        MicroOp inst = make_inst(OP_FADD_S);
        InstExec::FpuExecResult result = InstExec::exec_fpu(&inst);
        assert(result.valid);
        assert(result.unsupported);
    }

    return EXIT_SUCCESS;
}
