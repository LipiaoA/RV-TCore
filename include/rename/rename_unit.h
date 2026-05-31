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
    constexpr static u32 MAX_CHECKPOINT_DEPTH = 6;
public:
    RenameUnit(const RenameConfig& conf, const RenameDeps& deps);
    ~RenameUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
    bool                                dispatch(MInstPtr inst);
    void                                recycle_phys(int phys);
    void                                init_rat(const std::array<i32, RV_GPR_SIZE>& rat);

private:
    void                                flush();
    void                                r0();
    void                                r1();
    void                                r2();
    void                                rebuild_free_list();

    INPUT ASyncQueue<MInstPtr>&         m_decode_rename_q;
    OUTPUT ASyncQueue<MInstPtr>&        m_rename_issue_q;
    ROB&                                m_rob;
    RegFileUnit&                        m_regfile;
    std::array<i32, RV_GPR_SIZE>&       m_crat;
    std::vector<RatCheckpoint>&         m_checkpoints;

    FlushEvent&                         m_flush_event;

    std::array<i32, RV_GPR_SIZE>        m_rat;
    std::deque<i32>                     m_free_list;

    std::deque<MInstPtr>                m_r1_buf;
    std::deque<MInstPtr>                m_r2_buf;

    size_t                              m_rename_width { 4 };


    u64                                 m_checkpoint_id { 0 };
    u32                                 m_prf_capacity { 0 };
};

#endif
