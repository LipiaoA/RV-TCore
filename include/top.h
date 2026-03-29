#ifndef RV_TCORE_TOP_H
#define RV_TCORE_TOP_H

#include "intf.h"
#include "module_comm.h"

class CycleScheduler;
class MultiLevelTLB;
class GCache;
class DCache;
class ICache;
class BranchPredictor;
class FetchUnit;
class DecodeUnit;
class RenameUnit;
class CommitUnit;
class IssueUnit;
class ExecuteUnit;
class LSU;

class Top : public ICore {
public:
    explicit Top(const std::string& n);
    ~Top() override;

    void build() override;
    void tick() override;

private:
    std::vector<DevIntf*>                   m_queues;
    ModuleCommQueues                        m_comm_queues;

    CycleScheduler*                         m_sched { nullptr };
    MultiLevelTLB*                          m_tlb { nullptr };
    GCache*                                 m_l2 { nullptr };
    DCache*                                 m_dcache { nullptr };
    ICache*                                 m_icache { nullptr };
    BranchPredictor*                        m_bpu { nullptr };
    FetchUnit*                              m_fetch { nullptr };
    DecodeUnit*                             m_decode { nullptr };
    RenameUnit*                             m_rename { nullptr };
    CommitUnit*                             m_commit { nullptr };
    IssueUnit*                              m_issue { nullptr };
    ExecuteUnit*                            m_execute { nullptr };
    ROB*                                    m_rob { nullptr };
    LSU*                                    m_lsu { nullptr };
};

#endif
