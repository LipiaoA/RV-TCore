#ifndef RV_TCORE_RENAMEUNIT_H
#define RV_TCORE_RENAMEUNIT_H

#include <deque>
#include <vector>
#include <array>

#include "intf.h"
#include "config.h"
#include "decode/inst.h"
#include "module_comm.h"
#include "rename/prf.h"
#include "rename/checkpoint.h"

class ROB;

class RenameUnit : public IRename {
public:
    RenameUnit(const RenameConfig& conf, const RenameDeps& deps);
    ~RenameUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    bool                                dispatch(MInstPtr inst);
    void                                recycle_phys(int phys);

private:
    void                                r0();
    void                                r1();
    void                                r2();

    INPUT ASyncQueue<MInstPtr>&         m_decode_rename_q;
    OUTPUT ASyncQueue<MInstPtr>&        m_rename_issue_q;
    ROB&                                m_rob;
    RegFileUnit&                        m_regfile;


    FixedArray<int, RV_GPR_SIZE>        m_rat {};
    FixedArray<int, RV_GPR_SIZE>        m_crat {};
    std::deque<int>                     m_free_list;
    std::vector<RatCheckpoint>          m_checkpoints;

    std::deque<MInstPtr>                m_r1_buf;
    std::deque<MInstPtr>                m_r2_buf;

    size_t                              m_rename_width { 4 };
};

#endif
