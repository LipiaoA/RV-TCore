#ifndef RV_TCORE_OPCODE_H
#define RV_TCORE_OPCODE_H

typedef enum {
    RV_FMT_R,
    RV_FMT_I,
    RV_FMT_S,
    RV_FMT_B,
    RV_FMT_U,
    RV_FMT_J,
    RV_FMT_A, // stomic
    RV_FMT_K,
    RV_FMT_C, // CSR
    RV_FMT_H, // hfence

    // 不需要提取立即数/或由专用字段解释（如 fence/csr）
    RV_FMT_NONE
} InstFmt;

#include "opcode.inc"

inline bool is_load(InstOp op) {
    return (op >= OP_LB && op <= OP_LD) || op == OP_LDU;
}

inline bool is_fp(InstOp op) {
    return op >= OP_FLW && op <= OP_FMV_D_X;
}

inline bool is_int(InstOp op) {
    return op == OP_LUI ||
           op == OP_AUIPC ||
           (op >= OP_ADDI && op <= OP_AND) ||
           (op >= OP_ADDIW && op <= OP_SRAD);
}

inline bool is_store(InstOp op) {
    return (op >= OP_SB && op <= OP_SD) || op == OP_SQ;
}

inline bool is_load_store(InstOp op) {
    return is_load(op) || is_store(op);
}

inline bool ucond_jmp(InstOp op) {
    return op >= OP_JAL && op <= OP_JALR;
}

inline bool cond_jump(InstOp op) {
    return op >= OP_BEQ && op <= OP_BGEU;
}

inline bool is_branch(InstOp op) {
    return ucond_jmp(op) || cond_jump(op);
}

inline bool is_vector(InstOp op) {
    return op >= OP_VLE8_V && op < OP_AMOCAS_W;
}

#endif