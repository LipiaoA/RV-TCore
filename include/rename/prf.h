#ifndef RV_TCORE_PRF_H
#define RV_TCORE_PRF_H

#include "rv_def.h"
#include <array>
#include <cassert>
#include "module_comm.h"

class PRF {
    constexpr static u32 MAX_PHY_REG_SIZE = 512;
public:
    PRF(u32 phys_size = 192);
    ~PRF() = default;

    void                                reset();
    u64                                 read(i32 pidx) const;
    bool                                write(i32 pidx, u64 val);
    bool                                is_ready(i32 pidx) const;
    void                                set_busy(i32 pidx);

private:
    void                                mark_valid(i32 pidx);
    bool                                is_valid(i32 pidx) const;

private:
    std::array<u64, MAX_PHY_REG_SIZE>       m_regs {};
    std::array<bool, MAX_PHY_REG_SIZE>      m_valid {};
    u32                                     m_phys_n { 192 };
    bool                                    m_busy[MAX_PHY_REG_SIZE];
};

#endif // RV_TCORE_PRF_H
