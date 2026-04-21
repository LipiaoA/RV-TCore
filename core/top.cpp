#include "top.h"

#include "branch_predictor.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "rob/rob.h"
#include "log.h"

#include "icache/dcache.h"
#include "icache/icache.h"
#include "memory/tlb.h"
#include "memory/cache.h"
#include "util/util.h"

Top::Top(const TCoreConfig& config) : m_config(config) {
    init();
}

Top::Top()
 : m_config(TCoreConfig::get_instance()) {
    init();
}

Top::~Top() {
    delete_ptr(m_icache);
    delete_ptr(m_dcache);
    delete_ptr(m_l2);
    delete_ptr(m_dtlb);
    delete_ptr(m_itlb);
    delete_ptr(m_sched);
}

void Top::init()
{
    return;
}

void Top::build() {
    LOG_LEVEL1("TOP", "top building ...");

    m_comm_queues.fetch_to_decode    = new ASyncQueue<FetchPacket>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.decode_to_rename   = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.rename_to_issue    = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.issue_to_execute   = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.execute_to_commit  = new ASyncQueue<ExecResult>(DEFAULT_QUEUE_CAPACITY);
    const std::string dev_name = "top";
    const CacheConfig l2_conf           = m_config.l2_config();
    const CacheConfig ic_conf           = m_config.icache_config();
    const CacheConfig dc_conf           = m_config.dcache_config();
    std::vector<PTWLevelConfig> levels  = {{3}, {5}, {7}};

    ROBConfig       rob_conf        = m_config.rob_config();
    FetchConfig     fetch_conf      = m_config.fetch_config();
    DecodeConfig    decode_conf     = m_config.decode_config();
    RenameConfig    rename_conf     = m_config.rename_config();
    IssueConfig     issue_conf      = m_config.issue_config();
    ExecConfig      exec_conf       = m_config.execute_config();
    CommitConfig    commit_conf     = m_config.commit_config();
    LSUConfig       lsu_conf        = m_config.lsu_config();

    m_sched             =   new CycleScheduler();
    m_mem               =   new Memory();
    m_bpu               =   new BranchPredictor("bpu");

    m_l2                =   new DCache(m_sched, l2_conf);
    m_l2->set_name("l2 Cache");
    m_l2->set_mem(m_mem);

    m_dtlb              = new MultiLevelTLB(m_sched, 12, levels);
    m_itlb              = new MultiLevelTLB(m_sched, 12, levels);
    m_icache            = new ICache(m_sched, ic_conf, m_itlb);

    m_icache->attach_lower(m_l2);
    m_icache->set_name("ICache");

    m_dcache = new DCache(m_sched, dc_conf);
    m_dcache->attach_lower(m_l2);
    m_dcache->set_name("DCache");

    ROBDeps rob_deps;
    rob_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_rob = new ROB(rob_conf, rob_deps);

    IFetchDeps fetch_deps;
    fetch_deps.sched = m_sched;
    fetch_deps.icache = m_icache;
    fetch_deps.bpu = m_bpu;
    fetch_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    m_fetch = new FetchUnit(fetch_conf, fetch_deps);

    DecodeDeps decode_deps;
    decode_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    decode_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    m_decode = new DecodeUnit(decode_conf, decode_deps);


    RenameDeps rename_deps;
    rename_deps.rob = m_rob;
    rename_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    rename_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    m_rename = new RenameUnit(rename_conf, rename_deps);

    CommitDeps commit_deps;
    commit_deps.rob = m_rob;
    commit_deps.rename = m_rename;
    commit_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_commit = new CommitUnit(commit_conf, commit_deps);

    IssueDeps issue_deps;
    issue_deps.exec = m_execute;
    issue_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    issue_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    m_issue = new IssueUnit(issue_conf, issue_deps);

    LSUDeps lsu_deps;
    lsu_deps.rob = m_rob;
    lsu_deps.dcache = m_dcache;
    lsu_deps.dtlb = m_dtlb;
    lsu_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    lsu_deps.execute_commit_q = m_comm_queues.execute_to_commit;


    m_lsu = new LSU(lsu_conf, lsu_deps);

    ExecDeps exec_deps;
    exec_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    exec_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    exec_deps.lsu = m_lsu;

    m_execute = new ExecuteUnit(exec_conf, exec_deps);
}


CoreDeps Top::get_core_deps() const {
    CoreDeps deps;
    deps.fetch   = m_fetch;
    deps.decode  = m_decode;
    deps.rename  = m_rename;
    deps.issue   = m_issue;
    deps.execute = m_execute;
    deps.commit  = m_commit;
    deps.sched   = m_sched;
    return deps;
}
