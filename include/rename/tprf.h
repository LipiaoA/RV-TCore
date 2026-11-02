#ifndef RV_TCORE_TPRF_H
#define RV_TCORE_TPRF_H

#include "rv_def.h"
#include "rename/tile_reg.h"
#include "module_comm.h"
#include <cassert>

/*
 * TPRF is a tile register file.
 * It is a 2D array of tile registers.
 * The tile registers are indexed by a physical index.
 * The physical index is the index of the tile register in the TPRF.
 * The physical index is also the index of the tile register in the RAT.
 * The physical index is also the index of the tile register in the RS.
 */

class TPRF {
public:
    TPRF(u32 phys_size, u32 rows, u32 cols, u32 elem_bits);
    ~TPRF() = default;

    void reset();
    const TileReg& read(i32 pidx) const;
    bool write(i32 pidx, const TileReg& val);
    bool is_ready(i32 pidx) const;
    void set_busy(i32 pidx);

private:
    FixedArray<TileReg, MAX_TPRF_ENTRIES> m_regs {};
    u32 m_phys_n { 16 };
    u32 m_rows { TILE_ROWS };
    u32 m_cols { TILE_COLS };
    u32 m_elem_bits { TILE_ELEM_BITS };
    bool m_busy[MAX_TPRF_ENTRIES];
};

#endif
