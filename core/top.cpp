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
#include "flush_unit.h"
#include "regfile_unit.h"

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
    delete_ptr(m_mem);
    delete_ptr(m_bpu);
    delete_ptr(m_rob);
    delete_ptr(m_fetch);
    delete_ptr(m_decode);
    delete_ptr(m_rename);
    delete_ptr(m_issue);
    delete_ptr(m_regfile);
    delete_ptr(m_execute);
    delete_ptr(m_commit);
    delete_ptr(m_flush);
    delete_ptr(m_lsu);
    delete_ptr(m_comm_queues.fetch_to_decode);
    delete_ptr(m_comm_queues.decode_to_rename);
    delete_ptr(m_comm_queues.rename_to_issue);
    delete_ptr(m_comm_queues.rename_to_prf);
    delete_ptr(m_comm_queues.flush_req_q);
    delete_ptr(m_comm_queues.flush_event);
    delete_ptr(m_comm_queues.alu_resp_q);
    delete_ptr(m_comm_queues.bru_resp_q);
    delete_ptr(m_comm_queues.lsu_resp_q);
    delete_ptr(m_comm_queues.fpu_resp_q);
    delete_ptr(m_comm_queues.reg_req_q);
    delete_ptr(m_comm_queues.reg_bypass_bus);
    delete_ptr(m_comm_queues.inst_commit_q);
}

void Top::init()
{
    return;
}

void Top::build() {
    LOG_LEVEL1("TOP", "top building ...");

    m_comm_queues.fetch_to_decode    = new ASyncQueue<FetchPacket>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.decode_to_rename   = new ASyncQueue<MInstPtr>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.rename_to_issue    = new ASyncQueue<MInstPtr>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.rename_to_prf      = new ASyncQueue<RegReq>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.flush_req_q        = new ASyncQueue<FlushEvent>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.reg_req_q          = new ASyncQueue<RegReq>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.inst_commit_q      = new ASyncQueue<MInstPtr>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.reg_bypass_bus     = new FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>();

    m_comm_queues.alu_resp_q         = new FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>();
    m_comm_queues.bru_resp_q         = new FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>();
    m_comm_queues.lsu_resp_q         = new FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>();
    m_comm_queues.fpu_resp_q         = new FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY>();
    m_comm_queues.wakeup_bus         = new std::vector<WakeupEvent>();

    m_comm_queues.flush_event      = new FlushEvent();

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
    BPConfig        bp_conf         = m_config.bp_config();
    RegFileConfig   regfile_conf    = m_config.regfile_config();
    FlushUnitConfig flush_conf      = m_config.flush_config();

    m_sched             =   new CycleScheduler();
    m_mem               =   new Memory();
    m_bpu               =   new BranchPredictor(bp_conf);
    m_crat              =   new std::array<int, RV_GPR_SIZE>();
    m_checkpoints       =   new std::vector<RatCheckpoint>();

    m_crat->fill(0);
    (*m_crat)[2] = 1; // x2/sp uses a dedicated initial physical register.

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
    rob_deps.flush_event = m_comm_queues.flush_event;
    m_rob = new ROB(rob_conf, rob_deps);

    IFetchDeps fetch_deps;
    fetch_deps.sched = m_sched;
    fetch_deps.icache = m_icache;
    fetch_deps.bpu = m_bpu;
    fetch_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    fetch_deps.flush_event = m_comm_queues.flush_event;
    fetch_deps.perf_stats = &m_perf_stats;
    m_fetch = new FetchUnit(fetch_conf, fetch_deps);

    DecodeDeps decode_deps;
    decode_deps.fetch_decode_q = m_comm_queues.fetch_to_decode;
    decode_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    decode_deps.flush_event = m_comm_queues.flush_event;
    m_decode = new DecodeUnit(decode_conf, decode_deps);

    RegFileDeps regfile_deps;
    regfile_deps.reg_req_q = m_comm_queues.reg_req_q;
    regfile_deps.alu_resp_q = m_comm_queues.alu_resp_q;
    regfile_deps.bru_resp_q = m_comm_queues.bru_resp_q;
    regfile_deps.lsu_resp_q = m_comm_queues.lsu_resp_q;
    regfile_deps.fpu_resp_q = m_comm_queues.fpu_resp_q;
    regfile_deps.wakeup_bus = m_comm_queues.wakeup_bus;
    regfile_deps.flush_event = m_comm_queues.flush_event;
    m_regfile = new RegFileUnit(regfile_conf, regfile_deps);

    RenameDeps rename_deps;
    rename_deps.rob = m_rob;
    rename_deps.decode_rename_q = m_comm_queues.decode_to_rename;
    rename_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    rename_deps.flush_event = m_comm_queues.flush_event;
    rename_deps.regfile = m_regfile;
    rename_deps.crat = m_crat;
    rename_deps.checkpoints = m_checkpoints;
    rename_deps.perf_stats = &m_perf_stats;
    m_rename = new RenameUnit(rename_conf, rename_deps);

    CommitDeps commit_deps;
    commit_deps.rob = m_rob;
    commit_deps.rename = m_rename;
    commit_deps.inst_commit_q = m_comm_queues.inst_commit_q;
    commit_deps.flush_event = m_comm_queues.flush_event;
    commit_deps.flush_req_q = m_comm_queues.flush_req_q;
    commit_deps.reg_req_q = m_comm_queues.reg_req_q;
    commit_deps.crat = m_crat;
    commit_deps.checkpoints = m_checkpoints;
    commit_deps.perf_stats = &m_perf_stats;
    m_commit = new CommitUnit(commit_conf, commit_deps);



    LSUDeps lsu_deps;
    lsu_deps.rob = m_rob;
    lsu_deps.dcache = m_dcache;
    lsu_deps.dtlb = m_dtlb;
    lsu_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    lsu_deps.flush_event = m_comm_queues.flush_event;
    lsu_deps.reg_req_q = m_comm_queues.reg_req_q;
    lsu_deps.reg_resp_q = m_comm_queues.lsu_resp_q;
    lsu_deps.reg_bypass_bus = m_comm_queues.reg_bypass_bus;
    lsu_deps.flush_req_q = m_comm_queues.flush_req_q;

    m_lsu = new LSU(lsu_conf, lsu_deps);

    ExecDeps exec_deps;
    exec_deps.flush_event = m_comm_queues.flush_event;
    exec_deps.inst_commit_q = m_comm_queues.inst_commit_q;
    exec_deps.rob = m_rob;
    ALUDeps alu_deps;
    alu_deps.reg_req_q = m_comm_queues.reg_req_q;
    alu_deps.reg_resp_q = m_comm_queues.alu_resp_q;
    alu_deps.reg_bypass_bus = m_comm_queues.reg_bypass_bus;
    alu_deps.wakeup_bus = m_comm_queues.wakeup_bus;
    alu_deps.rob = m_rob;
    BRUDeps bru_deps;
    bru_deps.reg_req_q = m_comm_queues.reg_req_q;
    bru_deps.reg_resp_q = m_comm_queues.bru_resp_q;
    bru_deps.flush_req_q = m_comm_queues.flush_req_q;
    bru_deps.reg_bypass_bus = m_comm_queues.reg_bypass_bus;
    bru_deps.wakeup_bus = m_comm_queues.wakeup_bus;
    bru_deps.rob = m_rob;

    FPUDeps fpu_deps;
    fpu_deps.reg_req_q = m_comm_queues.reg_req_q;
    fpu_deps.reg_resp_q = m_comm_queues.fpu_resp_q;
    exec_deps.alu_deps = &alu_deps;
    exec_deps.bru_deps = &bru_deps;
    exec_deps.lsu_deps = &lsu_deps;
    exec_deps.fpu_deps = &fpu_deps;
    exec_deps.flush_event = m_comm_queues.flush_event;
    exec_deps.lsu = m_lsu;
    exec_deps.perf_stats = &m_perf_stats;

    m_execute = new ExecuteUnit(exec_conf, exec_deps);

    IssueDeps issue_deps;
    issue_deps.exec = m_execute;
    issue_deps.rename_issue_q = m_comm_queues.rename_to_issue;
    issue_deps.rob = m_rob;
    issue_deps.flush_event = m_comm_queues.flush_event;
    issue_deps.wakeup_bus = m_comm_queues.wakeup_bus;
    issue_deps.reg_req_q = m_comm_queues.reg_req_q;
    issue_deps.perf_stats = &m_perf_stats;
    m_issue = new IssueUnit(issue_conf, issue_deps);
    FlushUnitDeps flush_deps;
    flush_deps.flush_event = m_comm_queues.flush_event;
    flush_deps.flush_req_q = m_comm_queues.flush_req_q;
    flush_deps.perf_stats = &m_perf_stats;
    m_flush = new FlushUnit(flush_conf, flush_deps);
}

CoreDeps Top::get_core_deps() {
    CoreDeps deps;
    deps.fetch   = m_fetch;
    deps.decode  = m_decode;
    deps.rename  = m_rename;
    deps.issue   = m_issue;
    deps.regfile = m_regfile;
    deps.execute = m_execute;
    deps.commit  = m_commit;
    deps.sched   = m_sched;
    deps.flush   = m_flush;
    deps.rob     = m_rob;
    deps.flush_event = m_comm_queues.flush_event;
    deps.perf_stats = &m_perf_stats;
    return deps;
}
