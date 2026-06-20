#ifndef RV_TCORE_TILE_REG_H
#define RV_TCORE_TILE_REG_H

#include "rv_def.h"
#include <vector>
#include <cstring>

struct TileReg {
    u32 rows { 0 };
    u32 cols { 0 };
    u32 elem_bits { 32 };
    std::vector<u8> data;
    bool valid { false };

    u32 byte_size() const
    {
        return rows * cols * (elem_bits / 8);
    }

    void reset(u32 tile_rows, u32 tile_cols, u32 bits)
    {
        rows = tile_rows;
        cols = tile_cols;
        elem_bits = bits;
        data.assign(byte_size(), 0);
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

#endif
