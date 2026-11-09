#ifndef RV_TCORE_BRANCHPREDICTOR_H
#define RV_TCORE_BRANCHPREDICTOR_H

#include <string>

#include "intf.h"
#include "btb.h"
#include "ras.h"
#include "bim.h"
#include "tage.h"

struct BranchUpdate {
    pc_t pc { 0 };
    bool taken { false };
    bool target_valid { false };
    pc_t target { 0 };
    BrType type { BrType::NONE };
    u64 ghr_snapshot { 0 };
    u32 ras_sp { 0 };
    bool tage_valid { false };
    bool tage_final { false };
    int tage_provider { -1 };
    int tage_alt_provider { -1 };
    bool tage_alt_pred { false };
};

class BranchPredictor {

public:
    BranchPredictor(const BPConfig& config);
    ~BranchPredictor();

public:
    void                                update(const BranchUpdate& info);
    void                                reset();

    // nslots 覆盖本次取指块（一组内的指令数），按 base+i*4 索引。
    branch_pred                         f1_pred(pc_t pc, u32 nslots);
    branch_pred                         f2_pred(pc_t pc, const branch_pred& f1, u32 nslots);
    TagePred                            f3_pred(pc_t pc);

    // 把投机 GHR/RAS 收回到这条指令之前，再按最终方向重放这一条。
    void                                resync(u64 ghr, u32 ras_sp, BrType type, bool taken, pc_t pc);
    void                                shift_cond(bool taken);
    u64                                 spec_ghr() const { return m_bim.ghr(); }

    // 分支解析失败：resync 后把该状态记成恢复锚点。
    void                                rollback(u64 ghr, u32 ras_sp, BrType type, bool taken, pc_t pc);
    // 非分支 flush：丢掉错误路径上的投机历史。
    void                                squash_to_anchor();

private:
    void                                note_anchor(const BranchUpdate& info);

private:
    u64                                 m_time;
    RAS                                 m_ras;
    BTB                                 m_ibtb;
    TAGE                                m_tage;
    BIM                                 m_bim;

    u32                                 m_fetch_width;
    u64                                 m_anchor_ghr { 0 };
    u32                                 m_anchor_ras { 0 };
    bool                                m_anchor_valid { false };

};

#endif
