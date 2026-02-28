#ifndef RV_TCORE_INST_H
#define RV_TCORE_INST_H

#include "isa/opcode.h"

typedef struct {
    uint32_t mask;
    uint32_t match;
    InstOp opcode;
    InstFmt format;
} InstPattern;

typedef struct {
    uint64_t raw_inst;
    uint64_t pc;

    InstOp type;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;
    int32_t imm;
} SInst;

typedef struct {
    InstOp op_type;

    bool reads_rs1;
    bool reads_rs2;
    bool writes_rd;
    bool is_mem;
    bool is_branch;
    int latency;
} SUop;

typedef struct {
    // 指向静态语义
    const SInst* sinst;

    // ROB 信息
    int rob_id;

    // 重命名后的物理寄存器
    int phys_rs1;
    int phys_rs2;
    int phys_rd;

    bool rs1_ready;
    bool rs2_ready;

    // 执行状态
    bool issued;
    bool executing;
    bool executed;
    bool committed;

    uint64_t result;

} DynInst;

#endif
