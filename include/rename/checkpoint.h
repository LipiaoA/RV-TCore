#ifndef RV_TCORE_CHECKPOINT_H
#define RV_TCORE_CHECKPOINT_H

#include "module_comm.h"
#include <array>

// 分支 checkpoint：用于错误预测时回滚 RAT/ROB 状态
struct RatCheckpoint {
    FixedArray<int, RV_GPR_SIZE> rat {};
    int rob_head { 0 };
    int rob_tail { 0 };
    int checkpoint_id { -1 };
};

#endif
