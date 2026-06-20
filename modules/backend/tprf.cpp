#include "rename/tprf.h"

TPRF::TPRF(u32 phys_size, u32 rows, u32 cols, u32 elem_bits)
    : m_phys_n(phys_size)
    , m_rows(rows)
    , m_cols(cols)
    , m_elem_bits(elem_bits)
{
    reset();
}

void TPRF::reset()
{
    for (u32 i = 0; i < MAX_TPRF_ENTRIES; ++i) {
        m_regs[i].reset(m_rows, m_cols, m_elem_bits);
        m_busy[i] = true;
    }

    for (u32 i = 0; i < m_phys_n && i < RV_TILE_SIZE; ++i) {
        m_busy[i] = false;
        m_regs[i].valid = true;
    }
}

const TileReg& TPRF::read(i32 pidx) const
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    return m_regs[pidx];
}

bool TPRF::write(i32 pidx, const TileReg& val)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_regs[pidx] = val;
    m_regs[pidx].valid = true;
    m_busy[pidx] = false;
    return true;
}

bool TPRF::is_ready(i32 pidx) const
{
    if (pidx < 0 || static_cast<u32>(pidx) >= m_phys_n) {
        return false;
    }
    return m_busy[pidx] == false;
}

void TPRF::set_busy(i32 pidx)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_busy[pidx] = true;
}
