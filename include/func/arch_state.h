#ifndef RV_TCORE_ARCH_STATE_H
#define RV_TCORE_ARCH_STATE_H

#include "rv_def.h"
#include <unordered_map>

struct ArchState {
    u64                 gpr[32] {0};
    pc_t                pc { 0 };
    bool                halt { false };
    std::unordered_map<u64, u64> sysreg;
};

#endif
