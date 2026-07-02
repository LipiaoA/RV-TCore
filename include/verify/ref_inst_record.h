#ifndef RV_TCORE_REF_INST_RECORD_H
#define RV_TCORE_REF_INST_RECORD_H

#include "decode/inst.h"
#include "rv_def.h"

enum class RefEffect : u8 {
    NONE,
    GPR_WRITE,
    LOAD,
    STORE,
    BRANCH,
    FENCE,
    HALT,
    EXCEPTION,
    SKIP,
};

struct StepSnapshot {
    RefEffect   effect { RefEffect::NONE };
    u32         rd { 0 };
    u64         rd_val { 0 };
    u64         mem_addr { 0 };
    u64         mem_data { 0 };
    u32         mem_width { 0 };
    bool        branch_taken { false };
    pc_t        branch_target { 0 };
    pc_t        next_pc { 0 };
    u64         exc_cause { 0 };
    pc_t        exc_tval { 0 };
};

struct RetireMemInfo {
    bool        valid { false };
    u64         addr { 0 };
    u64         data { 0 };
    u32         width { 0 };
};

struct RefInstRecord {
    u64         seq { 0 };
    pc_t        pc { 0 };
    u32         raw_inst { 0 };
    InstOp      opcode { static_cast<InstOp>(0) };
    InstType    type { InstType::NONE };
    RefEffect   effect { RefEffect::NONE };

    u32         rd { 0 };
    u64         rd_val { 0 };

    u64         mem_addr { 0 };
    u64         mem_data { 0 };
    u32         mem_width { 0 };

    pc_t        next_pc { 0 };
    bool        branch_taken { false };
    pc_t        branch_target { 0 };

    u64         exc_cause { 0 };
    pc_t        exc_tval { 0 };
};

RefInstRecord ref_record(u64 seq, const MicroOp& uop, const StepSnapshot& snap);

#endif
