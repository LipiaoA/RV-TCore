#ifndef RV_TCORE_VPRF_H
#define RV_TCORE_VPRF_H

#include "rv_def.h"
#include "rename/vec_reg.h"
#include "module_comm.h"
#include <cassert>

class VPRF {
public:
    VPRF(u32 phys_size, u32 vlmax_bytes);
    ~VPRF() = default;

    void reset();
    const VecReg& read(i32 pidx) const;
    bool write(i32 pidx, const VecReg& val);
    bool is_ready(i32 pidx) const;
    void set_busy(i32 pidx);

private:
    FixedArray<VecReg, MAX_VPRF_ENTRIES> m_regs {};
    u32 m_phys_n { 64 };
    u32 m_vlmax_bytes { VLEN_BYTES };
    bool m_busy[MAX_VPRF_ENTRIES];
};

#endif
