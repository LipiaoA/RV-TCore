#ifndef RV_TCORE_TOP_H
#define RV_TCORE_TOP_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"

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
class ROB;

class Top {
public:
    explicit Top();
    explicit Top(const TCoreConfig& config);
    ~Top();

    void build();

private:
    void init();

private:
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
    TCoreConfig                             m_config;
};

#endif
