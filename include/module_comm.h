#ifndef RV_TCORE_MODULE_COMM_H
#define RV_TCORE_MODULE_COMM_H

#include <array>

#include "tools/queue.h"
#include "rv_def.h"
#include "decode/inst.h"

// 简化寄存器配置：32 逻辑寄存器，64 物理寄存器（含额外的重命名空间）
constexpr int RV_GPR_SIZE  = 32;
constexpr int RV_PHY_REG_SIZE = 64;

struct FetchInst {
    pc_t pc { 0 };
    insn_t insn { 0 };
    bool valid { false };
    u64 fetch_id { 0 };  // 用于 squash 精确识别
    u8  size { 4 };      // 指令字节数（支持 2/4/8 等变长）
};

struct FetchPacket {
    std::vector<FetchInst> insts;
    bool valid { false };
    pc_t base_pc { 0 };
    u64 fetch_id { 0 };

    bool pred_taken { false };
    pc_t pred_target { 0 };
    uint8_t  valid_mask { 0 };
    uint8_t  br_mask    { 0 };

    PredecodeInfo  predecode[MAX_FETCH_WIDTH];
};

struct ExecResult {
    DecodeInst inst;
    bool valid { false };
    u64 exec_id { 0 };
    int rob_id { -1 };
    u64 result { 0 };
};

struct CommitAck {
    u64 commit_id { 0 };
};

typedef struct{
    bool valid { false };
    bool write { false };
    i32 ptag { -1 };
    u64 val { 0 };
} RegReq;

typedef struct {
    bool valid { false };
    i32 tag { -1 };
} WakeupEvent;

enum class FlushType {
    NORMAL,
    BRANCH,
};

typedef struct {
    bool valid { false };
    ROBID rob_id { 0 };
    FlushType type { FlushType::NORMAL };
} FlushEvent;

enum LdStState {
    WAIT_ADDR,        // 等 AGU
    WAIT_SQ_CHECK,    // 等 store queue 检查
    WAIT_TLB,         // TLB 查询中
    WAIT_CACHE,       // Cache 查询中
    WAIT_FILL,        // Miss 等待 fill
    READY,          // 可写回
};

typedef struct {
    bool valid = false;
    uint64_t store_id;
    ROBID rob_idx;
    LdStState state {WAIT_ADDR};

    bool addr_ready = false;
    bool data_ready = false;

    uint64_t addr;
    uint64_t data;

    bool committed = false;
} SQEntry;

typedef struct {
    bool valid = false;
    MicroOpPtr uop { nullptr };
    LdStState state {WAIT_ADDR};
    ROBID rob_idx;

    bool addr_ready = false;
    uint64_t addr;

    bool executed = false;
    bool waiting_on_store = false;

    uint64_t wait_store_id = -1;
} LQEntry;

typedef struct {
    bool valid { false };
    uint64_t addr;
    uint64_t data;
    uint64_t store_idx;
} StoreReq;

typedef struct {
    bool valid { false };
    uint64_t addr;
    uint64_t load_idx;
} LoadReq;

typedef struct {
    bool valid { false };
    MicroOpPtr inst { nullptr };
} FUStage;

// 各段通信队列的指针集合，由 Top 在 build() 中填充并传入各模块
struct ModuleCommQueues {
    ASyncQueue<FetchPacket>*        fetch_to_decode   { nullptr };  // Fetch 写 -> Decode 读
    ASyncQueue<MicroOp>*            decode_to_rename  { nullptr };  // Decode 写 -> Rename 读
    ASyncQueue<MicroOp>*            rename_to_issue{ nullptr };  // Rename 写 -> Dispatch 读
    ASyncQueue<MicroOp>*            issue_to_execute  { nullptr };  // Issue 写 -> Execute 读
    ASyncQueue<ExecResult>*         execute_to_commit { nullptr };  // Execute 写 -> Commit/ROB 读
};

class ICache;
class GCache;
class MultiLevelTLB;
class CycleScheduler;
class BranchPredictor;
class ROB;
class RenameUnit;
class LSU;
class FetchUnit;
class DecodeUnit;
class RenameUnit;
class CommitUnit;
class IssueUnit;
class ExecuteUnit;

typedef struct {
    CycleScheduler*             sched;
    ICache*                     icache;
    BranchPredictor*            bpu;
    ASyncQueue<FetchPacket>*    fetch_decode_q;
} IFetchDeps;

typedef struct {
    ASyncQueue<FetchPacket>*    fetch_decode_q;
    ASyncQueue<MicroOp>*        decode_rename_q;
} DecodeDeps;

typedef struct {
    ROB*                        rob;
    ASyncQueue<MicroOp>*        decode_rename_q;
    ASyncQueue<MicroOp>*        rename_issue_q;
} RenameDeps;

typedef struct {
    ASyncQueue<ExecResult>*     execute_commit_q;
    ROB*                        rob;
    RenameUnit*                 rename;
} CommitDeps;

typedef struct {
    ExecuteUnit*                exec;
    ASyncQueue<MicroOp>*        rename_issue_q;
    ASyncQueue<MicroOp>*        issue_execute_q;
} IssueDeps;

typedef struct {
    LSU*                        lsu;
    ASyncQueue<MicroOp>*        issue_execute_q;
    ASyncQueue<ExecResult>*     execute_commit_q;
} ExecDeps;

typedef struct {
    ASyncQueue<ExecResult>*     execute_commit_q;
} ROBDeps;

typedef struct {
    ROB*                        rob;
    GCache*                     dcache;
    MultiLevelTLB*              dtlb;
    ASyncQueue<MicroOp>*        rename_issue_q;
    ASyncQueue<ExecResult>*     execute_commit_q;
} LSUDeps;

typedef struct {
    FetchUnit*                  fetch;
    DecodeUnit*                 decode;
    RenameUnit*                 rename;
    IssueUnit*                  issue;
    ExecuteUnit*                execute;
    CommitUnit*                 commit;
    CycleScheduler*             sched;
} CoreDeps;

#endif // RV_TCORE_MODULE_COMM_H
