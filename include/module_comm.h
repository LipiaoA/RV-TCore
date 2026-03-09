#ifndef RV_TCORE_MODULE_COMM_H
#define RV_TCORE_MODULE_COMM_H

#include "tools/queue.h"
#include "rv_def.h"
#include "decode/inst.h"

struct FetchInst {
    pc_t pc { 0 };
    insn_t insn { 0 };
    bool valid { false };
    u64 fetch_id { 0 };  // 用于 squash 精确识别
    u8  size { 4 };      // 指令字节数（支持 2/4/8 等变长）
};

struct FetchPacket {
    pc_t base_pc { 0 };                    // 起始 PC
    std::vector<FetchInst> insts;       // insts.size() <= k_fetch_width
    bool valid { false };
    u64 fetch_id { 0 };
    // 预测相关元数据
    bool pred_taken { false };
    pc_t pred_target { 0 };
};

struct DecodedInst {
    SInst inst;
    bool valid { false };
    u64 decode_id { 0 };
};

struct ExecResult {
    SInst inst;
    bool valid { false };
    u64 exec_id { 0 };
};

struct CommitAck {
    u64 commit_id { 0 };
};

// 模块间异步队列类型
using ModuleAsyncQueue =

// 各段通信队列的指针集合，由 Core 在 build() 中填充并传入各模块
struct ModuleCommQueues {
    ASyncQueue<FetchPacket>*        fetch_to_decode   { nullptr };  // Fetch 写 -> Decode 读
    ASyncQueue<DecodedInst>*        decode_to_rename  { nullptr };  // Decode 写 -> Rename 读
    ASyncQueue<DynInst>*            rename_to_dispatch{ nullptr };  // Rename 写 -> Dispatch 读
    ASyncQueue<DynInst>*            dispatch_to_issue { nullptr };  // Dispatch 写 -> Issue 读
    ASyncQueue<DynInst>*            issue_to_execute  { nullptr };  // Issue 写 -> Execute 读
    ASyncQueue<ExecResult>*         execute_to_commit { nullptr };  // Execute 写 -> Commit/ROB 读
};

#endif // RV_TCORE_MODULE_COMM_H
