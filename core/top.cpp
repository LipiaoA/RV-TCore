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

namespace {

CacheConfig to_gcache_cfg(const ICacheConfig& c) {
    CacheConfig cc(static_cast<size_t>(c.size), static_cast<size_t>(c.line_size),
                   static_cast<size_t>(c.ways), static_cast<size_t>(c.fetch_width));
    cc.hit_latency       = c.hit_latency;
    cc.miss_penalty      = c.miss_penalty;
    cc.install_latency   = c.install_latency;
    cc.writeback_latency = c.writeback_latency;
    return cc;
}

CacheConfig to_gcache_cfg(const DCacheConfig& c) {
    CacheConfig cc(static_cast<size_t>(c.size), static_cast<size_t>(c.line_size),
                   static_cast<size_t>(c.ways), static_cast<size_t>(c.fetch_width));
    cc.hit_latency       = c.hit_latency;
    cc.miss_penalty      = c.miss_penalty;
    cc.install_latency   = c.install_latency;
    cc.writeback_latency = c.writeback_latency;
    return cc;
}

CacheConfig to_gcache_cfg(const L2Config& c) {
    CacheConfig cc(static_cast<size_t>(c.size), static_cast<size_t>(c.line_size),
                   static_cast<size_t>(c.ways), static_cast<size_t>(c.fetch_width));
    cc.hit_latency       = c.hit_latency;
    cc.miss_penalty      = c.miss_penalty;
    cc.install_latency   = c.install_latency;
    cc.writeback_latency = c.writeback_latency;
    return cc;
}

InclusionPolicy to_inclusion(bool inclusive) {
    return inclusive ? InclusionPolicy::Inclusive : InclusionPolicy::NonInclusive;
}

} // namespace

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
    delete_ptr(m_tlb);
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
    m_comm_queues.rename_to_dispatch = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.dispatch_to_issue  = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.issue_to_execute   = new ASyncQueue<MicroOp>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.execute_to_commit  = new ASyncQueue<ExecResult>(DEFAULT_QUEUE_CAPACITY);

    const std::string dev_name = "top";

    m_sched = new CycleScheduler();

    const L2Config        l2_conf  = m_config.l2_config();
    const ICacheConfig    i_conf   = m_config.icache_config();
    const DCacheConfig    d_conf   = m_config.dcache_config();
    const CacheConfig     l2cfg    = to_gcache_cfg(l2_conf);

    m_l2 = new DCache(m_sched, l2cfg, to_inclusion(l2_conf.inclusive));
    m_l2->set_name("l2 Cache");

    std::vector<PTWLevelConfig> levels = {{3}, {5}, {7}};
    m_tlb = new MultiLevelTLB(m_sched, 12, levels);

    const CacheConfig icfg = to_gcache_cfg(i_conf);

    m_icache = new ICache(m_sched, icfg, m_tlb);
    m_icache->attach_lower(m_l2);
    m_icache->set_name("ICache");

    const CacheConfig dcfg = to_gcache_cfg(d_conf);

    m_dcache = new DCache(m_sched, dcfg, to_inclusion(d_conf.inclusive));
    m_dcache->attach_lower(m_l2);
    m_dcache->set_name("DCache");

    ROBConfig rob_conf = m_config.rob_config();

    ROBDeps rob_deps;
    rob_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    m_rob = new ROB(rob_conf, rob_deps);

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
    FetchConfig fetch_conf = m_config.fetch_config();
    m_fetch = new FetchUnit(fetch_conf, fetch_deps);

    DecodeDeps decode_deps;
    decode_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    decode_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    DecodeConfig decode_conf = m_config.decode_config();
    m_decode = new DecodeUnit(decode_conf, decode_deps);


    RenameDeps rename_deps;
    rename_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    rename_deps.rob = m_rob;
    rename_deps.integer_issue_q = m_comm_queues.dispatch_to_issue;
    rename_deps.ls_issue_q = m_comm_queues.dispatch_to_issue;
    rename_deps.fp_issue_q = m_comm_queues.dispatch_to_issue;
    RenameConfig rename_conf = m_config.rename_config();
    m_rename = new RenameUnit(rename_conf,rename_deps);

    CommitDeps commit_deps;
    commit_deps.rob = m_rob;
    commit_deps.rename = m_rename;
    commit_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    CommitConfig commit_conf = m_config.commit_config();
    m_commit = new CommitUnit(commit_conf, commit_deps);

    IssueDeps issue_deps;
    issue_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    issue_deps.dispatch_issue_q = m_comm_queues.dispatch_to_issue;
    IssueConfig issue_conf = m_config.issue_config();
    m_issue = new IssueUnit(issue_conf, issue_deps);

    ExecDeps exec_deps;
    exec_deps.issue_execute_q = m_comm_queues.issue_to_execute;
    exec_deps.execute_commit_q = m_comm_queues.execute_to_commit;
    ExecConfig exec_conf = m_config.execute_config();

    m_execute = new ExecuteUnit(exec_conf, exec_deps);


    LSUDeps lsu_deps;
    lsu_deps.rob = m_rob;
    lsu_deps.dcache = m_dcache;
    lsu_deps.tlb = m_tlb;
    lsu_deps.dispatch_issue_q = m_comm_queues.dispatch_to_issue;
    lsu_deps.execute_commit_q = m_comm_queues.execute_to_commit;

    LSUConfig lsu_conf = m_config.lsu_config();

    m_lsu = new LSU(lsu_conf, lsu_deps);
}
