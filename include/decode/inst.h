#ifndef RV_TCORE_INST_H
#define RV_TCORE_INST_H

#include "isa/opcode.h"
#include <cstdint>

struct PredecodeInfo {
    bool     valid    { false };
    bool     is_branch{ false };
    bool     is_load  { false };
    bool     is_store { false };
    bool     is_fp    { false };
    u8  length   { 4 };   // 指令字节数，当前仅支持 4
    u8  slot     { 0 };    // 在 line 内的 slot 索引
};

typedef struct {
    uint32_t mask;
    uint32_t match;
    InstOp opcode;
    InstFmt format;
} InstPattern;

typedef struct {
    u64 val { 0 };
    bool valid { false };
    i32 atag;
    i32 ptag;
    bool ready { false };
} RegType;

typedef struct {
    u32 idx;
    u32 direct {0};
} ROBID;

typedef struct {
    InstOp opcode;
    u64 raw_inst;
    u64 pc;
    bool valid { false };
    InstFmt format;
    RegType rs1;
    RegType rs2;
    RegType rd;
    i64 imm;
    u64 inst_id { 0 };
    ROBID rob_id {};
    bool issued { false };
} DecodeInst;

enum class InstType {
    STA = 0,
    STD,
    LOAD,
    STORE,
    BRANCH,
    INT,
    FP,
    NONE,
};

enum class InstState {
    NONE = 0,
    RENAMED = 1,
    ISSUED = 2,
    EXECUTED = 3,
    COMMITTED = 4,
    FLUSHED = 5,
    REPLAY = 6,
    RETIRED = 7,
};

typedef struct MicroOp {
    u64 raw_inst;
    u64 pc;
    std::string inst_name;
    InstState state { InstState::NONE };

    InstOp opcode;
    InstType type;
    bool valid { false };
    bool pred_taken { false };
    InstFmt format;
    RegType rs1;
    RegType rs2;
    RegType rd;
    i64 imm;
    u64 inst_id { 0 };
    ROBID rob_id {};
    i32 latency;
    uint64_t base;
    int64_t  offset;
    uint64_t addr;
    bool     addr_ready = false;
    uint64_t data;
    bool     data_ready = false;
    uint64_t store_idx;
    uint64_t load_idx;
    uint64_t load_data;
    bool executed = false;
    bool replay = false;
    uint32_t ssid = 0;

    bool rs1_ready = false;
    bool rs2_ready = false;

    bool ready() const {
        bool ready = true;
        if (rs1.valid) {
            ready &= rs1_ready;
        }
        if (rs2.valid) {
            ready &= rs2_ready;
        }
        return ready;
    }

} MicroOp, *MInstPtr;

using DynInst = MicroOp;

#endif
