
#ifndef RV_TCORE_COMMITUNIT_H
#define RV_TCORE_COMMITUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"

class ROB;
class RenameUnit;

class CommitUnit : public ICommit {
public:
    CommitUnit(const CommitConfig& conf, const CommitDeps& deps);
    ~CommitUnit() override = default;
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override;
private:
    void                                write_back(const MInstPtr inst);
    void                                inst_commit();
    void                                inst_reslove();

private:
    ASyncQueue<MInstPtr>&               m_inst_commit_q;
    ROB&                                m_rob;
    RenameUnit&                         m_rename;
    u32                                 m_commit_width;
    /* execute unit 的 commit 宽度 */
    u32                                 m_resolve_width;

    OUTPUT ASyncQueue<RegReq>&          m_reg_req_q;
    OUTPUT ASyncQueue<FlushEvent>&      m_flush_req_q;
    INPUT FlushEvent&                   m_flush_event;
};

#endif
