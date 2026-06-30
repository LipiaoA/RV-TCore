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

#endif
