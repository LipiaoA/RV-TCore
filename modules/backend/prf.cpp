#include "prf.h"

PRF::PRF(u32 phys_size)
  : m_phys_n(phys_size)
{
    reset();
}



void PRF::reset()
{
    m_regs.fill(0);
    m_valid.fill(false);

    memset(m_busy, true, m_phys_n * sizeof(bool));
    m_busy[0] = false;
    m_valid[0] = true;
}

u64 PRF::read(i32 pidx) const
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    return m_regs[pidx];
}

bool PRF::write(i32 pidx, u64 val)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    if (pidx == 0) {
        val = 0;
    }
    m_regs[pidx] = val;
    m_valid[pidx] = true;
    m_busy[pidx] = false;
    return true;
}

void PRF::mark_valid(i32 pidx)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_valid[pidx] = true;
}

bool PRF::is_valid(i32 pidx) const
{
    return pidx >= 0 && static_cast<u32>(pidx) < m_phys_n && m_valid[pidx];
}

bool PRF::is_ready(i32 pidx) const
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < MAX_PHY_REG_SIZE);
    return m_busy[pidx] == false;
}

void PRF::set_busy(i32 pidx)
{
    assert(pidx >= 0 && static_cast<u32>(pidx) < m_phys_n);
    m_busy[pidx] = true;
}
