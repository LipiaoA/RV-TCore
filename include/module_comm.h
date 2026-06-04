#ifndef RV_TCORE_MODULE_COMM_H
#define RV_TCORE_MODULE_COMM_H

#include <array>

#include "tools/queue.h"
#include "rv_def.h"
#include "decode/inst.h"

// 简化寄存器配置：32 逻辑寄存器，64 物理寄存器（含额外的重命名空间）
constexpr u32 RV_GPR_SIZE  = 32;
constexpr u32 TCORE_CHECKPOINT_SIZE  = 8;
constexpr u32 RV_PHY_REG_SIZE = 64;

enum class FuType {
    NONE,
    ALU,
    BRU,
    LSU,
    AGU,
    FPU,
};

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
    bool pred_taken { false };
    pc_t pred_target { 0 };
    uint8_t  valid_mask { 0 };
    uint8_t  br_mask    { 0 };

    PredecodeInfo  predecode[MAX_FETCH_WIDTH];
};

struct branch_pred_t {
    bool valid { false };
    bool taken { false };
    pc_t target { 0 };
};

constexpr static u32 MAX_PREDICT_WIDTH = 4;
typedef struct {
    branch_pred_t pred[MAX_PREDICT_WIDTH];
    u64 ghr_snapshot = 0;
} branch_pred;

struct CommitAck {
    u64 commit_id { 0 };
};

typedef struct{
    bool valid { false };
    bool write { false };
    i32 idx { -1 };
    i32 ptag { -1 };
    i32 atag { -1 };
    u64 val { 0 };
    ROBID rid { 0 };
    FuType fu { FuType::NONE };
} RegReq;

typedef struct{
    bool valid { false };
    i32 ptag { -1 };
    u64 val { 0 };
    ROBID rid { 0 };
    i32 idx { -1 };
    FuType fu { FuType::NONE };
    int fu_id { -1 };
} RegResp;

enum class FlushSource {
    NONE,
    IFETCH,
    DECODE,
    RENAME,
    ISSUE,
    EXECUTE,
    COMMIT,
    LSU,
    BRU,
};

enum class FlushType {
    NONE,
    MACHINE_CHECK,
    BRANCH_MISPRED,
    DEBUG,
    EXCEPTION,
    INTERRUPT,
    MEMORY_ORDER,
    LOAD_REPLAY,
    FENCE,
    FLUSH_TYPE_COUNT
};

constexpr static const char* FlushTypeStr[] = {
    "NONE",
    "MACHINE_CHECK",
    "BRANCH_MISPRED",
    "DEBUG",
    "EXCEPTION",
    "INTERRUPT",
    "MEMORY_ORDER",
    "LOAD_REPLAY",
    "FENCE",
};

constexpr static const char* FlushScopeStr[] = {
    "NONE",
    "YOUNGER_THAN",
    "FROM_RID",
    "ALL",
};

constexpr static const char* FlushSourceStr[] = {
    "NONE",
    "IFETCH",
    "DECODE",
    "RENAME",
    "ISSUE",
    "EXECUTE",
    "COMMIT",
    "LSU",
    "BRU",
};

enum class FlushScope {
    NONE,
    // 清除 rid 之后的年轻指令，保留 rid 本身。
    YOUNGER_THAN,
    // 清除 rid 及之后的指令。
    FROM_RID,
    // 全流水线清空，通常用于 exception/interrupt/reset。
    ALL,
};

enum WakeupType {
    WAKEUP_BYPASS,     // 可旁路
    WAKEUP_WRITEBACK,  // 已写回PRF
    WAKEUP_REPLAY,     // 依赖失效
    WAKEUP_KILL        // flush
};
typedef struct {
    bool       valid { false };
    WakeupType type;
    i32        ptag;
    ROBID      rid;
} WakeupEvent;

typedef struct {
    bool                valid { false };
    FlushType           type { FlushType::NONE };
    FlushSource         source { FlushSource::NONE };
    FlushScope          scope { FlushScope::NONE };
    ROBID               rid { 0 };
    bool                redirect { false };
    pc_t                redirect_pc { 0 };
    u64                 fetch_id { 0 };
    pc_t                inst_pc { 0 };
    // 分支恢复信息
    bool branch_taken { false };
    pc_t branch_target { 0 };
    bool recover_c_id { false };
    u64 c_id { 0 };

    bool has_exception { false };
    u64 exception_cause { 0 };
    u64 exception_tval { 0 };

    u64 replay_load_idx { 0 };
    u64 replay_store_idx { 0 };

    u64 epoch { 0 };
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
    MInstPtr uop { nullptr };
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
    MInstPtr uop { nullptr };
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
    MInstPtr inst { nullptr };
} FUStage;

struct RatCheckpoint {
    bool valid { false };
    std::array<int, RV_GPR_SIZE> rat { 0 };
    ROBID  rid { 0, 0 };
    std::deque<int> free_list;
    u64 c_id { 0 };
};

// 各段通信队列的指针集合，由 Top 在 build() 中填充并传入各模块
constexpr static u32 MAX_REGISTER_REQ_CAPACITY = 16;
struct ModuleCommQueues {
    /* 各模块间指令流水异步队列 */
    ASyncQueue<FetchPacket>*        fetch_to_decode   { nullptr };  // Fetch 写 -> Decode 读
    ASyncQueue<MInstPtr>*           decode_to_rename  { nullptr };  // Decode 写 -> Rename 读
    ASyncQueue<MInstPtr>*           rename_to_issue{ nullptr };     // Rename 写 -> Dispatch 读
    ASyncQueue<MInstPtr>*           issue_to_execute  { nullptr };  // Issue 写 -> Execute 读
    ASyncQueue<MInstPtr>*           inst_commit_q { nullptr };  // Execute 写 -> Commit/ROB 读
    ASyncQueue<RegReq>*             rename_to_prf     { nullptr };  // Rename 写 -> PRF 读

    /* 各模块的 register request 和 response 队列 */
    ASyncQueue<RegReq>*             reg_req_q { nullptr };  // RegFile 写 -> RegFile 读

    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*            alu_resp_q { nullptr };  // ALU 读 -> RegFile 写
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*            bru_resp_q { nullptr };  // BRU 读 -> RegFile 写
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*            lsu_resp_q { nullptr };  // LSU 读 -> RegFile 写
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*            fpu_resp_q { nullptr };  // FPU 读 -> RegFile 写

    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*            reg_bypass_bus { nullptr };

    /* 各模块的 flush 事件 */
    ASyncQueue<FlushEvent>*         flush_req_q { nullptr };  // 统一的 flush 请求队列
    FlushEvent*                     flush_event { nullptr };  // 统一的 flush 广播事件，由 FlushUnit 管理
    std::vector<WakeupEvent>*       wakeup_bus { nullptr };  // 统一的 wakeup 事件，由 IssueUnit 管理
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
class RegFileUnit;
class FlushUnit;
struct PerfStats;

typedef struct IFetchReq {
    bool valid { false };
    pc_t pc;
    pc_t line_addr;
    u64 epoch;
} IFetchReq;

constexpr u32 BANK_BYTES = 16;
typedef struct IFetchResp {
    bool valid { false };
    pc_t line_addr;
    u8 data[64];
    bool page_fault { false };
    bool access_fault { false };
    u64 epoch;
} IFetchResp;

typedef struct {
    CycleScheduler*             sched { nullptr };
    ICache*                     icache { nullptr };
    BranchPredictor*            bpu { nullptr };
    ASyncQueue<FetchPacket>*    fetch_decode_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
    PerfStats*                  perf_stats { nullptr };
} IFetchDeps;

typedef struct {
    ASyncQueue<FetchPacket>*    fetch_decode_q { nullptr };
    ASyncQueue<MInstPtr>*       decode_rename_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
} DecodeDeps;

typedef struct {
    ROB*                        rob { nullptr };
    ASyncQueue<MInstPtr>*       decode_rename_q { nullptr };
    ASyncQueue<MInstPtr>*       rename_issue_q { nullptr };
    RegFileUnit*                regfile { nullptr };
    FlushEvent*                 flush_event { nullptr };
    std::array<int, RV_GPR_SIZE>*   crat { nullptr };
    std::vector<RatCheckpoint>*     checkpoints { nullptr };
    PerfStats*                  perf_stats { nullptr };
} RenameDeps;

typedef struct {
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        alu_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        bru_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        lsu_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        fpu_resp_q { nullptr };
    std::vector<WakeupEvent>*   wakeup_bus { nullptr };
    FlushEvent*                 flush_event { nullptr };
} RegFileDeps;

typedef struct {
    ASyncQueue<MInstPtr>*       inst_commit_q { nullptr };
    ROB*                        rob { nullptr };
    RenameUnit*                 rename { nullptr };
    FlushEvent*                 flush_event { nullptr };
    ASyncQueue<FlushEvent>*     flush_req_q { nullptr };
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    std::array<int, RV_GPR_SIZE>*   crat { nullptr };
    std::vector<RatCheckpoint>*     checkpoints { nullptr };
    PerfStats*                  perf_stats { nullptr };
} CommitDeps;

typedef struct {
    ExecuteUnit*                exec { nullptr };
    ROB*                        rob { nullptr };
    ASyncQueue<MInstPtr>*       rename_issue_q { nullptr };
    std::vector<WakeupEvent>*   wakeup_bus { nullptr };
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
    PerfStats*                  perf_stats { nullptr };
} IssueDeps;

typedef struct {
    ROB*                        rob { nullptr };
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus { nullptr };
    std::vector<WakeupEvent>*   wakeup_bus { nullptr };
} ALUDeps;

typedef struct {
    ROB*                        rob { nullptr };
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    ASyncQueue<FlushEvent>*     flush_req_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus { nullptr };
    std::vector<WakeupEvent>*   wakeup_bus { nullptr };
} BRUDeps;

typedef struct {
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus { nullptr };
    FlushEvent*                 flush_event { nullptr };
    ROB*                        rob { nullptr };
    std::vector<WakeupEvent>*   wakeup_bus { nullptr };
} FPUDeps;

typedef struct {
    FlushEvent*                 flush_event { nullptr };
} ROBDeps;

typedef struct {
    ROB*                        rob { nullptr };
    GCache*                     dcache { nullptr };
    MultiLevelTLB*              dtlb { nullptr };
    ASyncQueue<MInstPtr>*       rename_issue_q { nullptr };
    ASyncQueue<RegReq>*         reg_req_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
    ASyncQueue<FlushEvent>*     flush_req_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_resp_q { nullptr };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>*        reg_bypass_bus { nullptr };
} LSUDeps;

typedef struct {
    LSU*                        lsu { nullptr };
    ASyncQueue<MInstPtr>*       inst_commit_q { nullptr };
    ASyncQueue<FlushEvent>*     flush_req_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
    ALUDeps*                    alu_deps { nullptr };
    BRUDeps*                    bru_deps { nullptr };
    LSUDeps*                    lsu_deps { nullptr };
    FPUDeps*                    fpu_deps { nullptr };
    ROB*                        rob { nullptr };
    PerfStats*                  perf_stats { nullptr };
} ExecDeps;

typedef struct {
    ASyncQueue<FlushEvent>*     flush_req_q { nullptr };
    FlushEvent*                 flush_event { nullptr };
    PerfStats*                  perf_stats { nullptr };
} FlushUnitDeps;

typedef struct {
    FetchUnit*                  fetch { nullptr };
    DecodeUnit*                 decode { nullptr };
    RenameUnit*                 rename { nullptr };
    IssueUnit*                  issue { nullptr };
    RegFileUnit*                regfile { nullptr };
    ExecuteUnit*                execute { nullptr };
    CommitUnit*                 commit { nullptr };
    CycleScheduler*             sched { nullptr };
    FlushUnit*                  flush { nullptr };
    ROB*                        rob { nullptr };
    FlushEvent*                 flush_event { nullptr };
    PerfStats*                  perf_stats { nullptr };
} CoreDeps;

#endif // RV_TCORE_MODULE_COMM_H
