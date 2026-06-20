#ifndef RV_TCORE_VEC_REG_H
#define RV_TCORE_VEC_REG_H

#include "rv_def.h"
#include <vector>
#include <cstring>

struct VecReg {
    u32 vlen { 0 };
    u32 vlmax_bytes { 0 };
    std::vector<u8> data;
    bool valid { false };

    void reset(u32 capacity_bytes)
    {
        vlmax_bytes = capacity_bytes;
        vlen = capacity_bytes;
        data.assign(capacity_bytes, 0);
        valid = false;
    }

    void clear_data()
    {
        if (!data.empty()) {
            std::memset(data.data(), 0, data.size());
        }
        valid = false;
    }
};

inline bool vec_regs_equal_bytes(const VecReg& lhs, const VecReg& rhs, u32 nbytes)
{
    if (lhs.data.size() < nbytes || rhs.data.size() < nbytes) {
        return false;
    }
    return std::memcmp(lhs.data.data(), rhs.data.data(), nbytes) == 0;
}

#endif
