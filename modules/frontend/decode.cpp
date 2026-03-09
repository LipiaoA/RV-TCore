#include "decode.h"

static int64_t extract_imm(uint32_t raw, InstFmt fmt) {

    switch (fmt) {

    case RV_FMT_I:
        return sign_extend(raw >> 20, 12);

    case RV_FMT_S: {
        uint32_t imm =
            ((raw >> 7) & 0x1f) |
            ((raw >> 25) << 5);
        return sign_extend(imm, 12);
    }

    case RV_FMT_B: {
        uint32_t imm =
            ((raw >> 8) & 0x0f) << 1 |
            ((raw >> 25) & 0x3f) << 5 |
            ((raw >> 7) & 0x1) << 11 |
            ((raw >> 31) << 12);
        return sign_extend(imm, 13);
    }

    case RV_FMT_U:
        return (int64_t)(raw & 0xfffff000);

    case RV_FMT_J: {
        uint32_t imm =
            ((raw >> 21) & 0x3ff) << 1 |
            ((raw >> 20) & 0x1) << 11 |
            ((raw >> 12) & 0xff) << 12 |
            ((raw >> 31) << 20);
        return sign_extend(imm, 21);
    }

    default:
        return 0;
    }
}

bool rv64i_decode(uint64_t raw, uint64_t pc, Sinst *out) {
    for (size_t i = 0; i < rv64i_table_size; i++) {
        if ((raw & rv64i_table[i].mask) == rv64i_table[i].match) {
            out->raw = raw;
            out->pc = pc;
            out->opcode = rv64i_table[i].opcode;
            out->format = rv64i_table[i].format;

            out->rd  = (raw >> 7)  & 0x1f;
            out->rs1 = (raw >> 15) & 0x1f;
            out->rs2 = (raw >> 20) & 0x1f;

            out->imm = extract_imm(raw, out->format);

            return true;
        }
    }

    out->opcode = OP_INVALID;
    return false;
}

