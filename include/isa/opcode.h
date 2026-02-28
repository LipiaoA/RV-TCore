#ifndef RV_TCORE_OPCODE_H
#define RV_TCORE_OPCODE_H

typedef enum {
    RV_FMT_R,
    RV_FMT_I,
    RV_FMT_S,
    RV_FMT_B,
    RV_FMT_U,
    RV_FMT_J,
    // 不需要提取立即数/或由专用字段解释（如 fence/csr）
    RV_FMT_NONE
} InstFmt;

typedef enum {
    /* -------------------- RV32/64I Base Integer ISA -------------------- */
    OP_ADD,
    OP_SUB,
    OP_SLL,
    OP_SLT,
    OP_SLTU,
    OP_XOR,
    OP_SRL,
    OP_SRA,
    OP_OR,
    OP_AND,

    OP_ADDI,
    OP_SLTI,
    OP_SLTIU,
    OP_XORI,
    OP_ORI,
    OP_ANDI,
    OP_SLLI,
    OP_SRLI,
    OP_SRAI,

    OP_LB,
    OP_LH,
    OP_LW,
    OP_LD,
    OP_LBU,
    OP_LHU,
    OP_LWU,

    OP_SB,
    OP_SH,
    OP_SW,
    OP_SD,

    OP_BEQ,
    OP_BNE,
    OP_BLT,
    OP_BGE,
    OP_BLTU,
    OP_BGEU,

    OP_LUI,
    OP_AUIPC,
    OP_JAL,
    OP_JALR,

    OP_FENCE,
    OP_FENCE_I,

    OP_ECALL,
    OP_EBREAK,

    OP_CSRRW,
    OP_CSRRS,
    OP_CSRRC,
    OP_CSRRWI,
    OP_CSRRSI,
    OP_CSRRCI,

    /* -------------------- RV64I (in addition to RV32I) -------------------- */
    OP_ADDIW,
    OP_SLLIW,
    OP_SRLIW,
    OP_SRAIW,
    OP_ADDW,
    OP_SUBW,
    OP_SLLW,
    OP_SRLW,
    OP_SRAW,

    /* -------------------- RV32/64M (Integer Multiply/Divide) -------------------- */
    OP_MUL,
    OP_MULH,
    OP_MULHSU,
    OP_MULHU,
    OP_DIV,
    OP_DIVU,
    OP_REM,
    OP_REMU,

    OP_MULW,
    OP_DIVW,
    OP_DIVUW,
    OP_REMW,
    OP_REMUW,

    OP_INVALID

} InstOp;

#endif