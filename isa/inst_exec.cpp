#include <cassert>

#include "inst_exec.h"
#include "isa/opcode.h"
#include "decode/inst.h"

namespace InstExec {

AluExecResult exec_alu(MInstPtr inst) {
    AluExecResult result;
    result.valid = true;
    result.write_rd = inst->rd.valid;
    switch (inst->opcode) {
    case OP_ADD: {
        result.value = inst->rs1.val + inst->rs2.val;
    } break;
    case OP_SUB: {
        result.value = inst->rs1.val - inst->rs2.val;
    } break;
    case OP_AND: {
        result.value = inst->rs1.val & inst->rs2.val;
    } break;
    case OP_OR: {
        result.value = inst->rs1.val | inst->rs2.val;
    } break;
    case OP_XOR: {
        result.value = inst->rs1.val ^ inst->rs2.val;
    } break;
    case OP_SLL: {
        result.value = inst->rs1.val << inst->rs2.val;
    } break;
    case OP_SRL: {
        result.value = inst->rs1.val >> inst->rs2.val;
    } break;
    case OP_SRA: {
        result.value = inst->rs1.val >> inst->rs2.val;
    } break;
    case OP_SLT: {
        result.value = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->rs2.val);
    } break;
    case OP_SLTU: {
        result.value = inst->rs1.val < inst->rs2.val;
    } break;
    case OP_ADDI: {
        result.value = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SLTI: {
        result.value = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->imm);
    } break;
    case OP_SLTIU: {
        result.value = inst->rs1.val < static_cast<u64>(inst->imm);
    } break;
    case OP_XORI: {
        result.value = inst->rs1.val ^ static_cast<u64>(inst->imm);
    } break;
    case OP_ORI: {
        result.value = inst->rs1.val | static_cast<u64>(inst->imm);
    } break;
    case OP_ANDI: {
        result.value = inst->rs1.val & static_cast<u64>(inst->imm);
    } break;
    case OP_SLLI: {
        result.value = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLI: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAI: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDIW: {
        result.value = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SLLIW: {
        result.value = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLIW: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAIW: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDW: {
        result.value = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SUBW: {
        result.value = inst->rs1.val - static_cast<u64>(inst->imm);
    } break;
    case OP_SLLW: {
        result.value = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLW: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAW: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDID: {
        result.value = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_SLLID: {
        result.value = inst->rs1.val << static_cast<u64>(inst->imm);
    } break;
    case OP_SRLID: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_SRAID: {
        result.value = inst->rs1.val >> static_cast<u64>(inst->imm);
    } break;
    case OP_ADDD: {
        result.value = inst->rs1.val + static_cast<u64>(inst->imm);
    } break;
    case OP_AUIPC: {
        result.value = inst->pc + inst->imm;
    } break;
    case OP_LUI: {
        result.value = inst->imm;
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
    default:
        result.valid = false;
        assert(false);
        break;
    }
    return result;
}

BruExecResult exec_bru(MInstPtr inst) {
    BruExecResult result;
    result.valid = true;
    switch (inst->opcode) {
        case OP_BEQ: {
            result.taken= inst->rs1.val == inst->rs2.val;
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_BNE: {
            result.taken = inst->rs1.val != inst->rs2.val;
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_BLT: {
            result.taken = static_cast<i64>(inst->rs1.val) < static_cast<i64>(inst->rs2.val);
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_BGE: {
            result.taken = static_cast<i64>(inst->rs1.val) >= static_cast<i64>(inst->rs2.val);
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_BLTU: {
            result.taken = inst->rs1.val < inst->rs2.val;
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_BGEU: {
            result.taken = inst->rs1.val >= inst->rs2.val;
            result.target = inst->pc + static_cast<u64>(inst->imm);
        } break;
        case OP_JAL: {
            result.taken = true;
            result.target = inst->pc + static_cast<u64>(inst->imm);
            result.write_rd = inst->rd.valid;
            result.link = inst->pc + 4;
        } break;
        case OP_JALR: {
            result.taken = true;
            result.target = (inst->rs1.val + static_cast<u64>(inst->imm)) & ~1ull;
            result.write_rd = inst->rd.valid;
            result.link = inst->pc + 4;
        } break;
        default:
            result.valid = false;
            break;
    }
    return result;
}

AguExecResult exec_agu(MInstPtr inst) {
    AguExecResult result;
    result.valid = true;
    result.addr = inst->rs1.val + static_cast<u64>(inst->imm);
    return result;
}

FpuExecResult exec_fpu(MInstPtr inst) {
    FpuExecResult result;
    (void)inst;
    result.valid = true;
    result.unsupported = true;
    return result;
}

}
