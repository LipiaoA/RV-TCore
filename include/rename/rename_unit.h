#ifndef RV_TCORE_RENAMEUNIT_H
#define RV_TCORE_RENAMEUNIT_H

#include "intf.h"
#include "decode/inst.h"
#include "module_comm.h"
#include "rename/prf.h"
#include "rename/checkpoint.h"
#include <deque>
#include <vector>
#include <array>

class ROB;

class RenameUnit : public IRename {
public:
    RenameUnit(const RenameDeps& deps);
    ~RenameUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    bool                                dispatch(MicroOp& inst);
    void                                recycle_phys(int phys);

private:
    void                                r0();
    void                                r1();
    void                                r2();

    ASyncQueue<MicroOp>&                m_decode_rename_q;
    ROB&                                m_rob;
    ASyncQueue<MicroOp>&                m_integer_issue_q;
    ASyncQueue<MicroOp>&                m_ls_issue_q;
    ASyncQueue<MicroOp>&                m_fp_issue_q;

    std::array<int, RV_GPR_SIZE>        m_rat {};
    std::array<int, RV_GPR_SIZE>        m_crat {};
    std::deque<int>                     m_free_list;
    std::vector<RatCheckpoint>          m_checkpoints;
    PRF                                 m_prf;

    std::deque<MicroOp>                 m_r1_buf;
    std::deque<MicroOp>                 m_r2_buf;

    size_t                              m_rename_width { 4 };
};

#endif
