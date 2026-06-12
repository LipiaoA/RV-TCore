#include "rename/vprf.h"

VPRF::VPRF(u32 phys_size, u32 vlmax_bytes)
    : m_phys_n(phys_size)
    , m_vlmax_bytes(vlmax_bytes)
{
    reset();
}

void VPRF::reset()
{
    for (u32 i = 0; i < MAX_VPRF_ENTRIES; ++i) {
        m_regs[i].reset(m_vlmax_bytes);
        m_busy[i] = true;
    }

    for (u32 i = 0; i < m_phys_n && i < RV_VREG_SIZE; ++i) {
        m_busy[i] = false;
        m_regs[i].valid = true;
    }
    m_busy[0] = false;
    m_regs[0].valid = true;
}

const VecReg& VPRF::read(i32 pidx) const
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    return m_regs[pidx];
}

bool VPRF::write(i32 pidx, const VecReg& val)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_regs[pidx] = val;
    m_regs[pidx].valid = true;
    m_busy[pidx] = false;
    return true;
}

bool VPRF::is_ready(i32 pidx) const
{
    if (pidx < 0 || static_cast<u32>(pidx) >= m_phys_n) {
        return false;
    }
    return m_busy[pidx] == false;
}

void VPRF::set_busy(i32 pidx)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_busy[pidx] = true;
}
