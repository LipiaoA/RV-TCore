#include <cassert>

#include "inst_exec.h"
#include "isa/opcode.h"
#include "decode/inst.h"

namespace InstExec {

bool exec(MInstPtr inst) {
    switch (inst->opcode) {
    case OP_ADD: {
        inst->rd.val = inst->rs1.val + inst->rs2.val;
    } break;
    case OP_SUB: {
        inst->rd.val = inst->rs1.val - inst->rs2.val;
    } break;
    case OP_AND: {
        inst->rd.val = inst->rs1.val & inst->rs2.val;
    } break;
    case OP_OR: {
        inst->rd.val = inst->rs1.val | inst->rs2.val;
    } break;
    case OP_XOR: {
        inst->rd.val = inst->rs1.val ^ inst->rs2.val;
    } break;
    case OP_SLL: {
        inst->rd.val = inst->rs1.val << inst->rs2.val;
    } break;
    case OP_SRL: {
        inst->rd.val = inst->rs1.val >> inst->rs2.val;
    } break;
    case OP_SRA: {
        inst->rd.val = inst->rs1.val >> inst->rs2.val;
    } break;
    case OP_SLT: {
        inst->rd.val = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->rs2.val);
    } break;
    case OP_SLTU: {
        inst->rd.val = inst->rs1.val < inst->rs2.val;
    } break;
    case OP_BEQ: {
        inst->rd.val = inst->rs1.val == inst->rs2.val;
    } break;
    case OP_BNE: {
        inst->rd.val = inst->rs1.val != inst->rs2.val;
    } break;
    case OP_BLT: {
        inst->rd.val = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->rs2.val);
    } break;
    case OP_BGE: {
        inst->rd.val = static_cast<i64>(inst->rs1.val) >= static_cast<i64>(inst->rs2.val);
    } break;
    case OP_BLTU: {
        inst->rd.val = inst->rs1.val < inst->rs2.val;
    } break;
    case OP_BGEU: {
        inst->rd.val = inst->rs1.val >= inst->rs2.val;
    } break;
    case OP_JAL: {
        inst->rd.val = inst->rs1.val + inst->rs2.val;
    } break;
    case OP_JALR: {
        inst->rd.val = inst->rs1.val + inst->rs2.val;
    } break;
    case OP_ADDI: {
        inst->rd.val = inst->rs1.val + inst->imm;
    } break;
    case OP_SLTI: {
        inst->rd.val = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->imm);
    } break;
    case OP_SLTIU: {
        inst->rd.val = inst->rs1.val < static_cast<u64>(inst->imm);
    } break;
    case OP_XORI: {
        inst->rd.val = inst->rs1.val ^ static_cast<u64>(inst->imm);
    } break;
    case OP_ORI: {
        inst->rd.val = inst->rs1.val | static_cast<u64>(inst->imm);
    } break;
    case OP_ANDI: {
        inst->rd.val = inst->rs1.val & static_cast<u64>(inst->imm);
    } break;
    case OP_SLLI: {
        inst->rd.val = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLI: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAI: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDIW: {
        inst->rd.val = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SLLIW: {
        inst->rd.val = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLIW: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAIW: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDW: {
        inst->rd.val = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SUBW: {
        inst->rd.val = inst->rs1.val - static_cast<u64>(inst->imm);
    } break;
    case OP_SLLW: {
        inst->rd.val = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLW: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAW: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDID: {
        inst->rd.val = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SLLID: {
        inst->rd.val = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLID: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAID: {
        inst->rd.val = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDD: {
        inst->rd.val = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SUBD:
    case OP_SLLD:
    case OP_SRLD:
    case OP_SRAD:
    case OP_MUL:
    case OP_MULH:
    case OP_MULHSU:
    case OP_MULHU:
    case OP_DIV:
    case OP_DIVU:
    case OP_REM:
    case OP_REMU:
    case OP_MULW:
    case OP_DIVW:
    case OP_DIVUW:
    case OP_REMW:
    case OP_REMUW:
    case OP_MULD:
    case OP_DIVD:
    case OP_DIVUD:
    case OP_REMD:
    case OP_REMUD:
    case OP_LR_W:
    case OP_SC_W:
    case OP_AMOSWAP_W:
    case OP_SSAMOSWAP_W:
    case OP_AMOADD_W:
    case OP_AMOXOR_W:
    case OP_AMOAND_W:
    case OP_AMOOR_W:
    case OP_AMOMIN_W:
    case OP_AMOMAX_W:
    case OP_AMOMINU_W:
    case OP_AMOMAXU_W:
    case OP_LR_D:
    case OP_SC_D:
    case OP_AMOSWAP_D:
    case OP_SSAMOSWAP_D:
    case OP_AMOADD_D:
    case OP_AMOXOR_D:
    case OP_AMOAND_D:
    case OP_AMOOR_D:
    case OP_AMOMIN_D:
    case OP_AMOMAX_D:
    case OP_AMOMINU_D:
    case OP_AMOMAXU_D:
    case OP_FLW:
    case OP_FSW:
    case OP_FMADD_S:
    case OP_FMSUB_S:
    case OP_FNMSUB_S:
    case OP_FNMADD_S:
    case OP_FADD_S:
    case OP_FSUB_S:
    case OP_FMUL_S:
    case OP_FDIV_S:
    case OP_FSQRT_S:
    case OP_FSGNJ_S:
    case OP_FSGNJN_S:
    case OP_FSGNJX_S:
    case OP_FMIN_S:
    case OP_FMAX_S:
    case OP_FCVT_W_S:
    case OP_FCVT_WU_S:
    case OP_FMV_X_W:
    case OP_FEQ_S:
    case OP_FLT_S:
    case OP_FLE_S:
    case OP_FCLASS_S:
    case OP_FCVT_S_W:
    case OP_FCVT_S_WU:
    case OP_FMV_W_X:
    case OP_FCVT_L_S:
    case OP_FCVT_LU_S:
    case OP_FCVT_S_L:
    case OP_FCVT_S_LU:
    case OP_FLD:
    case OP_FSD:
    case OP_FMADD_D:
    case OP_FMSUB_D:
    case OP_FNMSUB_D:
    case OP_FNMADD_D:
    case OP_FADD_D:
    case OP_FSUB_D:
    case OP_FMUL_D:
    case OP_FDIV_D:
    case OP_FSQRT_D:
    case OP_FSGNJ_D:
    case OP_FSGNJN_D:
    case OP_FSGNJX_D:
    case OP_FMIN_D:
    case OP_FMAX_D:
    case OP_FCVT_S_D:
    case OP_FCVT_D_S:
    case OP_FEQ_D:
    case OP_FLT_D:
    case OP_FLE_D:
    case OP_FCLASS_D:
    case OP_FCVT_W_D:
    case OP_FCVT_WU_D:
    case OP_FCVT_D_W:
    case OP_FCVT_D_WU:
    case OP_FCVT_L_D:
    case OP_FCVT_LU_D:
    case OP_FMV_X_D:
    case OP_FCVT_D_L:
    case OP_FCVT_D_LU:
    case OP_FMV_D_X:
        assert(0);
    default:
        assert(0);
        return false;
    }
    return true;
}

bool exec_alu(MInstPtr inst) {
    return exec(inst);
}

bool exec_bru(MInstPtr inst) {
    return exec(inst);
}

bool exec_agu(MInstPtr inst) {
    return exec(inst);
}

bool exec_fpu(MInstPtr inst) {
    return exec(inst);
}

}
