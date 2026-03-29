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

Top::Top(const std::string& n) : ICore(n) {}

Top::~Top() {
    for (auto* m : m_modules)
        delete_ptr(m);
    m_modules.clear();

    for (auto* q : m_queues)
        delete_ptr(q);
    m_queues.clear();

    delete_ptr(m_icache);
    delete_ptr(m_dcache);
    delete_ptr(m_l2);
    delete_ptr(m_tlb);
    delete_ptr(m_sched);
}

void Top::tick() {
    ICore::tick();
    for (auto& q : m_queues)
        q->tick();
}

void Top::build() {
    LOG_LEVEL1("TOP", "top building ...");

    m_comm_queues.fetch_to_decode    = new ASyncQueue<FetchPacket>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.decode_to_rename   = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.rename_to_dispatch = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.dispatch_to_issue  = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.issue_to_execute   = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.execute_to_commit  = new ASyncQueue<ExecResult>(DEFAULT_QUEUE_CAPACITY);

    m_queues.push_back(m_comm_queues.fetch_to_decode);
    m_queues.push_back(m_comm_queues.decode_to_rename);
    m_queues.push_back(m_comm_queues.rename_to_dispatch);
    m_queues.push_back(m_comm_queues.dispatch_to_issue);
    m_queues.push_back(m_comm_queues.issue_to_execute);
    m_queues.push_back(m_comm_queues.execute_to_commit);

    const std::string dev_name = "top";

    m_sched = new CycleScheduler();

    CacheConfig l2cfg(512 * 1024, 64, 8);
    l2cfg.hit_latency = 10;
    l2cfg.miss_penalty = 30;
    l2cfg.install_latency = 5;
    l2cfg.writeback_latency = 12;

    m_l2 = new DCache(m_sched, l2cfg, InclusionPolicy::Inclusive);
    m_l2->set_name("l2 Cache");

    std::vector<PTWLevelConfig> levels = {{3}, {5}, {7}};
    m_tlb = new MultiLevelTLB(m_sched, 12, levels);

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    icfg.hit_latency = 1;
    icfg.miss_penalty = 3;
    icfg.install_latency = 1;
    icfg.writeback_latency = 8;

    m_icache = new ICache(m_sched, icfg, m_tlb);
    m_icache->attach_lower(m_l2);
    m_icache->set_name("ICache");

    CacheConfig dcfg(64 * 1024, 64, 4);
    dcfg.hit_latency = 2;
    dcfg.miss_penalty = 8;
    dcfg.install_latency = 2;

    m_dcache = new DCache(m_sched, dcfg, InclusionPolicy::NonInclusive);
    m_dcache->attach_lower(m_l2);
    m_dcache->set_name("DCache");

    ExecConfig exec_conf;
    exec_conf.alu_num = 6;
    exec_conf.fpu_num = 2;
    exec_conf.bru_num = 1;

    LSUConfig lsu_conf;
    lsu_conf.load_q_size = 128;
    lsu_conf.store_q_size = 128;
    lsu_conf.agus_num = 4;
    lsu_conf.mshr_buffer_size = 128;
    lsu_conf.issue_width = 2;
    lsu_conf.dcache_hit_latency = 2;
    lsu_conf.dcache_miss_penalty = 8;
    lsu_conf.dcache_install_latency = 2;

    IssueConfig issue_conf;
    issue_conf.int_issue_width = 4;
    issue_conf.ls_issue_width = 2;
    issue_conf.fp_issue_width = 2;

    ROBConfig rob_conf;
    rob_conf.capacity = 64;
    ROBDeps rob_deps;
    rob_deps.conf = rob_conf;
    rob_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_rob = new ROB(rob_deps);

    auto* predictor = new BranchPredictor(dev_name);
    m_bpu = predictor;

    IFetchDeps fetch_deps;
    fetch_deps.sched = m_sched;
    fetch_deps.icache = m_icache;
    fetch_deps.dcache = m_dcache;
    fetch_deps.l2 = m_l2;
    fetch_deps.tlb = m_tlb;
    fetch_deps.bpu = predictor;
    fetch_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    m_fetch = new FetchUnit(fetch_deps);
    m_fetch->build();

    DecodeDeps decode_deps;
    decode_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    decode_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    m_decode = new DecodeUnit(decode_deps);

    RenameDeps rename_deps;
    rename_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    rename_deps.rob = m_rob;
    rename_deps.integer_issue_q = m_comm_queues.dispatch_to_issue;
    rename_deps.ls_issue_q = m_comm_queues.dispatch_to_issue;
    rename_deps.fp_issue_q = m_comm_queues.dispatch_to_issue;
    m_rename = new RenameUnit(rename_deps);

    CommitDeps commit_deps;
    commit_deps.rob = m_rob;
    commit_deps.rename = m_rename;
    commit_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_commit = new CommitUnit(commit_deps);

    IssueDeps issue_deps;
    issue_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    issue_deps.dispatch_issue_q = m_comm_queues.dispatch_to_issue;
    m_issue = new IssueUnit(issue_conf, issue_deps);

    ExecDeps exec_deps;
    exec_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    exec_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_execute = new ExecuteUnit(exec_conf, exec_deps);

    LSUDeps lsu_deps;
    lsu_deps.rob = m_rob;
    lsu_deps.dcache = m_dcache;
    lsu_deps.tlb = m_tlb;
    lsu_deps.dispatch_issue_q = m_comm_queues.dispatch_to_issue;
    lsu_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_lsu = new LSU(lsu_conf, lsu_deps);

    m_lsu->build();

    add_module(predictor);
    add_module(m_decode);
    add_module(m_commit);
    add_module(m_issue);
    add_module(m_execute);
    add_module(m_rob);
    add_module(m_rename);
    add_module(m_fetch);
}
