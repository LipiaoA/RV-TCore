#include "isa/decode.h"
#include "opcode.h"

#include "isa/isa_inst.def"
#include "util.h"

namespace isa {

u32 ls_width(InstOp op) {
    switch (op) {
    case OP_LB:
    case OP_LBU:
    case OP_SB:
        return 1;
    case OP_SH:
    case OP_LH:
    case OP_LHU:
        return 2;
    case OP_SW:
    case OP_LW:
    case OP_LWU:
        return 4;
    case OP_SD:
    case OP_LD:
        return 8;
    default: break;
    }
    return 0;
}

i64 extract_imm(u64 raw, InstFmt fmt, InstOp op)
{
    switch (fmt) {
    case RV_FMT_I:
        if (op == OP_SLLI || op == OP_SRLI || op == OP_SRAI) {
            return (raw >> 20) & 0x3f;
        }
        if (op == OP_SLLIW || op == OP_SRLIW || op == OP_SRAIW) {
            return (raw >> 20) & 0x1f;
        }
        return sextend64(raw >> 20, 12);
    case RV_FMT_S: {
        u64 imm = ((raw >> 7) & 0x1f) | ((raw >> 25) << 5);
        return sextend64(imm, 12);
    }
    case RV_FMT_B: {
        u64 imm = ((raw >> 8) & 0x0f) << 1 | ((raw >> 25) & 0x3f) << 5
                | ((raw >> 7) & 0x1) << 11 | ((raw >> 31) << 12);
        return sextend64(imm, 13);
    }
    case RV_FMT_U:
        return static_cast<i64>(raw & 0xfffff000ULL);
    case RV_FMT_J: {
        u64 imm = ((raw >> 21) & 0x3ff) << 1 | ((raw >> 20) & 0x1) << 11
                | ((raw >> 12) & 0xff) << 12 | ((raw >> 31) << 20);
        return sextend64(imm, 21);
    }
    default:
        return 0;
    }
}

void map_vec_ops(MicroOp* out)
{
    out->vd.valid = out->rd.valid;
    out->vd.atag = out->rd.atag;
    out->vd.rtyp = RegType::VEC;
    out->rd.valid = false;

    const u32 funct3 = static_cast<u32>((out->raw_inst >> 12) & 0x7);
    if (funct3 == 4) {
        out->vs2.valid = out->rs2.valid;
        out->vs2.atag = out->rs2.atag;
        out->vs2.rtyp = RegType::VEC;
        out->rs2.valid = false;
        out->rs1.rtyp = RegType::GPR;
        return;
    }
    if (funct3 == 3) {
        out->vs2.valid = out->rs2.valid;
        out->vs2.atag = out->rs2.atag;
        out->vs2.rtyp = RegType::VEC;
        out->rs2.valid = false;
        return;
    }
    out->vs1.valid = out->rs1.valid;
    out->vs1.atag = out->rs1.atag;
    out->vs1.rtyp = RegType::VEC;
    out->vs2.valid = out->rs2.valid;
    out->vs2.atag = out->rs2.atag;
    out->vs2.rtyp = RegType::VEC;
    out->rs1.valid = false;
    out->rs2.valid = false;
}

bool fill_operands(InstFmt fmt, MicroOp* out)
{
    switch (fmt) {
    case RV_FMT_R:
        out->rd.valid = true;
        out->rs1.valid = true;
        out->rs2.valid = true;
        break;
    case RV_FMT_I:
        out->rd.valid = true;
        out->rs1.valid = true;
        out->rs2.valid = false;
        break;
    case RV_FMT_S:
        out->rd.valid = false;
        out->rs1.valid = true;
        out->rs2.valid = true;
        break;
    case RV_FMT_B:
        out->rd.valid = false;
        out->rs1.valid = true;
        out->rs2.valid = true;
        break;
    case RV_FMT_U:
    case RV_FMT_J:
        out->rd.valid = true;
        out->rs1.valid = false;
        out->rs2.valid = false;
        break;
    case RV_FMT_A:
        out->rd.valid = true;
        out->rs1.valid = true;
        out->rs2.valid = true;
        break;
    case RV_FMT_C:
    case RV_FMT_K:
    case RV_FMT_H:
    case RV_FMT_NONE:
        out->rd.valid = false;
        out->rs1.valid = false;
        out->rs2.valid = false;
        out->opcode = OP_INVALID;
        return false;
    default:
        out->opcode = OP_INVALID;
        return false;
    }
    return true;
}

InstType inst_type(InstOp op) {
    InstType type = InstType::NONE;
    if (is_branch(op)) {
        type = InstType::BRANCH;
    } else if (is_load(op)) {
        type = InstType::LOAD;
    } else if (is_store(op)) {
        type = InstType::STORE;
    } else if (is_fp(op)) {
        type = InstType::FP;
    } else if (is_vector(op)) {
        type = InstType::VECTOR;
    } else if (is_int(op)) {
        type = InstType::INT;
    } else if (is_cmd(op)) {
        type = InstType::CMD;
    } else if (is_ecall(op)) {
        type = InstType::BRANCH;
    } else if (is_mul_div_rem(op)) {
        // FIXME: mul and div instructions are not implemented yet,
        // so we treat them as integer instructions for now.
        type = InstType::INT;
    }
    return type;
}

bool decode(u64 raw, MicroOp *out) {
    static const u32 rv64i_inst_size = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
    for (size_t i = 0; i < rv64i_inst_size; i++) {
        if ((raw & rv64i_table[i].mask) == rv64i_table[i].match) {
            const InstOp op   = rv64i_table[i].opcode;
            const InstFmt fmt = rv64i_table[i].format;
            auto name_it = rv64i_inst_map.find(op);
            if (name_it != rv64i_inst_map.end()) {
                out->inst_name = name_it->second;
            } else {
                out->inst_name.clear();
            }
            out->raw_inst = raw;
            out->opcode   = op;
            out->format   = fmt;
            out->type     = inst_type(op);
            out->ls_width = ls_width(op);
            out->rd.atag  = static_cast<i32>((raw >> 7) & 0x1f);
            out->rs1.atag = static_cast<i32>((raw >> 15) & 0x1f);
            out->rs2.atag = static_cast<i32>((raw >> 20) & 0x1f);
            fill_operands(fmt, out);
            if (is_vector(op)) {
                // Vector operand mapping not implemented yet; reject so the
                // caller reports an illegal insn instead of corrupting state.
                return false;
            }
            return true;
        }
    }
    out->opcode = OP_INVALID;
    return false;
}

void predecode(insn_t raw, PredecodeInfo* out)
{
    out->valid = false;
    out->is_branch = out->is_load = out->is_store = out->is_fp = false;
    out->length = 4;
    out->slot = 0;
    static const u32 rv64i_sz = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
    for (u32 i = 0; i < rv64i_sz; ++i) {
        if ((raw & rv64i_table[i].mask) != rv64i_table[i].match)
            continue;
        InstOp op = rv64i_table[i].opcode;
        out->valid = true;
        out->is_branch = is_branch(op);
        out->is_load = is_load(op);
        out->is_store = is_store(op);
        out->is_fp = is_fp(op);
        break;
    }
    return;
}

} // namespace isa
