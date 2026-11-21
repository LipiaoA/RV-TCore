#ifndef RV_TCORE_INST_H
#define RV_TCORE_INST_H

#include <cstdint>
#include <string>
#include <format>

#include "isa/opcode.h"
#include "rv_def.h"
#include "rename/vec_reg.h"
#include "rename/tile_reg.h"

struct PredecodeInfo {
    bool     valid    { false };
    bool     is_branch{ false };
    bool     is_load  { false };
    bool     is_store { false };
    bool     is_fp    { false };
    u8  length   { 4 };   // 指令字节数，当前仅支持 4
    u8  slot     { 0 };    // 在 line 内的 slot 索引
};

// 取指阶段定稿、随指令走到执行/flush 的分支预测
struct BranchPredMeta {
    bool valid { false };
    bool taken { false };
    bool target_valid { false };
    pc_t target { 0 };
    BrType type { BrType::NONE };
    u64 ghr_snapshot { 0 };
    u32 ras_sp { 0 };
    // F1/F2 已经按 spec_pc 做过的投机预测；F3 用它判断要不要纠正
    bool spec { false };
    pc_t spec_pc { 0 };
    bool tage_valid { false };
    bool tage_final { false };
    int tage_provider { -1 };
    int tage_alt_provider { -1 };
    bool tage_alt_pred { false };
};

typedef struct {
    uint32_t mask;
    uint32_t match;
    InstOp opcode;
    InstFmt format;
} InstPattern;

enum class RegType {
    GPR,
    VEC,
    TILE,
};

typedef struct {
    u64 val { 0 };
    bool valid { false };
    i32 atag;
    i32 ptag;
    bool ready { false };
    bool bypass_hint { false };
    bool bypass_ready { false };
    bool data_available {false};
    u32 speculative_child_mask { 0 };
    i32 o_ptag { -1 };
    RegType rtyp { RegType::GPR };
    u32 rgrp { 1 };
} Operand;

typedef struct ROBID {
    u32 idx;
    u32 direct {0};
    std::string s;
    const char * str() {
        s = (direct ? "--" : "++") + std::to_string(idx);
        return s.c_str();
    }
    bool operator ==(const struct ROBID& other) const {
        return idx == other.idx && direct == other.direct;
    }
    bool operator !=(const struct ROBID& other) const {
        return idx != other.idx || direct != other.direct;
    }
} ROBID;

typedef struct {
    InstOp opcode;
    u64 raw_inst;
    u64 pc;
    bool valid { false };
    InstFmt format;
    Operand rs1;
    Operand rs2;
    Operand rd;
    i64 imm;
    u64 fetch_id { 0 };
    ROBID rid {};
    bool issued { false };
} DecodeInst;

enum class InstType {
    NONE = 0,
    STA,
    STD,
    LOAD,
    STORE,
    BRANCH,
    INT,
    FP,
    CMD,
    VECTOR,
    TENSOR,
    NUM_INST_TYPES,
};

enum class InstState {
    NONE = 0,
    RENAMED = 1,
    ISSUED,
    FU_ACTIVE,
    EXECUTED,
    COMMITTED,
    FLUSHED,
    REPLAY,
    RETIRED,
};

enum class StoreIssueHalf {
    NONE = 0,
    STA,
    STD,
};

typedef struct MicroOp {
    u64 raw_inst;
    u64 pc;
    std::string inst_name;
    InstState state { InstState::NONE };
    bool next_valid { false };
    u32 inst_size {4}; // FIXME: should be modified

    InstOp opcode;
    InstType type { InstType::NONE };
    bool valid { false };
    bool pred_taken { false };
    BranchPredMeta bpred {};
    InstFmt format;
    Operand rs1;
    Operand rs2;
    Operand rd;
    Operand vs1;
    Operand vs2;
    Operand vd;
    Operand ts1;
    Operand ts2;
    Operand td;
    VecReg vd_payload;
    TileReg td_payload;
    u32 lmul { 1 };
    bool vm { true };
    i64 imm;
    u64 fetch_id { 0 };
    ROBID rid {};
    i32 latency;
    uint64_t base;
    int64_t  offset;
    uint64_t addr;
    bool     addr_ready { false };
    uint64_t data;
    bool     data_ready { false };
    uint64_t store_id;
    uint64_t load_idx;
    uint64_t load_data;
    u32 ls_width {0};
    bool executed = false;
    bool replay = false;
    // STORE 半发射/半完成（单 MicroOp，不再拆 STA/STD）
    bool sta_iss { false };
    bool std_iss { false };
    bool sta_done { false };
    bool std_done { false };
    StoreIssueHalf store_issue_half { StoreIssueHalf::NONE };
    u32 ssid { 0 };
    // checkpoint id
    u64 c_id { 0 };
    u64 epoch { 0 };

    bool fetch_fault { false };
    bool has_exception { false };
    u64 exception_cause { 0 };
    pc_t exception_tval { 0 };

    // 性能计数：每次成功发射（含 STORE 半发射 / replay 再发射）+1
    u32 perf_issued_uops { 0 };
    bool perf_accounted { false };

    bool sta_ready() const {
        return !rs1.valid || rs1.rtyp != RegType::GPR || rs1.ready;
    }

    bool std_ready() const {
        return !rs2.valid || rs2.rtyp != RegType::GPR || rs2.ready;
    }

    bool store_fully_issued() const {
        return sta_iss && std_iss;
    }

    bool ready() const {
        if (type == InstType::STORE) {
            return (!sta_iss && sta_ready()) || (!std_iss && std_ready());
        }
        bool ready = true;
        if (rs1.valid && rs1.rtyp == RegType::GPR) {
            ready &= rs1.ready;
        }
        if (rs2.valid && rs2.rtyp == RegType::GPR) {
            ready &= rs2.ready;
        }
        if (type == InstType::VECTOR) {
            if (vs1.valid) {
                ready &= vs1.ready;
            }
            if (vs2.valid) {
                ready &= vs2.ready;
            }
        }
        if (type == InstType::TENSOR) {
            if (ts1.valid) {
                ready &= ts1.ready;
            }
            if (ts2.valid) {
                ready &= ts2.ready;
            }
        }
        return ready;
    }

} MicroOp, *MInstPtr;

using DynInst = MicroOp;

const std::string gpr_name[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
    "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};
#endif
