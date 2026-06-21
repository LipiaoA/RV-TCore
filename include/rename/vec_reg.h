#ifndef RV_TCORE_VEC_REG_H
#define RV_TCORE_VEC_REG_H

#include "rv_def.h"
#include <cstring>

struct VecReg {
    bool    valid { false };
    u32     vlen { 0 };
    u32     vlmax_bytes { 0 };
    u8      data[VLEN_BYTES];
    u32     sz { VLEN_BYTES };

    void reset(u32 capacity_bytes)
    {
        vlmax_bytes = capacity_bytes;
        vlen = capacity_bytes;
        std::memset(data, 0, VLEN_BYTES);
        valid = false;
    }

    void clear_data()
    {
        std::memset(data, 0, VLEN_BYTES);
        valid = false;
    }

    bool equal(const VecReg& other) const
    {
        return vlen == other.vlen && std::memcmp(data, other.data, vlen) == 0;
    }

    u32 size() const
    {
        return sz;
    }
};

#endif
