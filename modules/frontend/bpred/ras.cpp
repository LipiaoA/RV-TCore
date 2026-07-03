#include <algorithm>
#include "ras.h"

RAS::RAS(u32 depth)
    : DEPTH(depth)
{
}

void RAS::push_spec(pc_t pc)
{
    if (m_sp < DEPTH)
        m_stack[m_sp++] = pc;
    else {
        std::rotate(m_stack.begin(), m_stack.begin() + 1, m_stack.end());
        m_stack[DEPTH - 1] = pc;
    }
}

void RAS::pop_spec()
{
    if (m_sp > 0) {
        m_sp--;
    }
}

void RAS::reset()
{
    m_sp = 0;
}

RAS::Checkpoint RAS::checkpoint() const
{
    return {m_sp};
}

void RAS::restore(const Checkpoint& cp)
{
    m_sp = cp.sp;
}

bool RAS::empty() const
{
    return m_sp == 0;
}

pc_t RAS::top() const
{
    if (m_sp > 0) {
        return m_stack[m_sp - 1];
    }
    return 0;
}
