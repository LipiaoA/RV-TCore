#ifndef RV_TCORE_PRF_H
#define RV_TCORE_PRF_H

#include "rv_def.h"
#include <array>
#include <cassert>
#include "module_comm.h"

class PRF {
public:
    PRF() {
        reset();
    }
public:
    std::array<u64, RV_PHY_REG_SIZE> m_regs {};
    std::array<bool, RV_PHY_REG_SIZE> m_valid {};

    void reset() {
        m_regs.fill(0);
        m_valid.fill(false);
        for (i32 i = 0; i < RV_GPR_SIZE; ++i) {
            m_valid[i] = true;
        }
    }

    u64 read(i32 pidx) const {
        if (!is_valid(pidx))
            return 0;
        return m_regs[pidx];
    }

    bool write(i32 pidx, u64 val) {
        if (pidx < 0 || pidx >= RV_PHY_REG_SIZE) {
            assert(false);
            return false;
        }
        m_regs[pidx] = val;
        m_valid[pidx] = true;
        return true;
    }

    void mark_valid(i32 pidx) {
        if (pidx < 0 || pidx >= RV_PHY_REG_SIZE) {
            assert(false);
            return;
        }
        m_valid[pidx] = true;
    }

    bool is_valid(i32 pidx) const {
        return pidx >= 0 && pidx < RV_PHY_REG_SIZE && m_valid[pidx];
    }
};

#endif // RV_TCORE_PRF_H
