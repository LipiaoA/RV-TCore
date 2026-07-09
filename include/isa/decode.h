#ifndef RV_TCORE_ISA_DECODE_H
#define RV_TCORE_ISA_DECODE_H

#include "decode/inst.h"
#include "opcode.h"

namespace isa {
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

inline bool is_cmd(InstOp op) {
    return op >= OP_WFI && op <= OP_CSRRW;
}

inline bool is_ecall(InstOp op) {
    return op == OP_ECALL;
}

inline bool is_vector(InstOp op) {
    return op >= OP_VLE8_V && op < OP_AMOCAS_W;
}

InstType inst_type(InstOp op);
u32      ls_width(InstOp op);
i64      extract_imm(u64 raw, InstFmt fmt, InstOp op);
bool     decode(u64 raw, MicroOp* out);
void     predecode(u64 raw, PredecodeInfo* out);

}

#endif
