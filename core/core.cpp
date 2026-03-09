#include "core.h"
#include "branch_predictor.h"
#include "rob.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "dispatch_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "branch_predictor.h"
#include "log.h"

Core::~Core() = default;

void Core::tick() {
    ICore::tick();
    for (auto& q : m_queues)
        q->tick();
}

void Core::build() {
    auto& log = LogSystem::instance();
    LOG_LEVEL1("CORE", "core building ...");

    m_comm_queues.fetch_to_decode    = new ASyncQueue<FetchPacket>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.decode_to_rename   = new ASyncQueue<DecodedInst>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.rename_to_dispatch = new ASyncQueue<DynInst>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.dispatch_to_issue  = new ASyncQueue<DynInst>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.issue_to_execute   = new ASyncQueue<DynInst>(DEFAULT_QUEUE_CAPACITY);
    m_comm_queues.execute_to_commit  = new ASyncQueue<ExecResult>(DEFAULT_QUEUE_CAPACITY);

    m_queues.push_back(m_comm_queues.fetch_to_decode);  // fetch_to_decode
    m_queues.push_back(m_comm_queues.decode_to_rename);  // decode_to_rename
    m_queues.push_back(m_comm_queues.rename_to_dispatch);  // rename_to_dispatch
    m_queues.push_back(m_comm_queues.dispatch_to_issue);  // dispatch_to_issue
    m_queues.push_back(m_comm_queues.issue_to_execute);  // issue_to_execute
    m_queues.push_back(m_comm_queues.execute_to_commit);  // execute_to_commit

    std::string dev_name = "unknow";
    IRob* rob = new ROB("rob");
    rob->build();
    FetchUnit* fetch = new FetchUnit(dev_name);
    fetch->build();
    add_module(fetch);

    CommitUnit* commit = new CommitUnit(dev_name);
    DecodeUnit* decode = new DecodeUnit(dev_name);
    DispatchUnit* dispatch = new DispatchUnit(dev_name);
    ExecuteUnit* exec = new ExecuteUnit(dev_name);
    IssueUnit* issue = new IssueUnit(dev_name);
    RenameUnit* rename = new RenameUnit(dev_name);
    BranchPredictor* predictor = new BranchPredictor(dev_name);

    add_module(predictor);
    add_module(decode);
    add_module(commit);
    add_module(issue);
    add_module(exec);
    add_module(rob);
    add_module(rename);
    add_module(dispatch);

    fetch->set_fetch_decode_q(m_comm_queues.fetch_to_decode);
    decode->set_fetch_decode_q(m_comm_queues.fetch_to_decode);
    rename->set_decode_rename_q(m_comm_queues.decode_to_rename);
    dispatch->set_rename_dispatch_q(m_comm_queues.rename_to_dispatch);
    issue->set_dispatch_issue_q(m_comm_queues.dispatch_to_issue);
    exec->set_issue_execute_q(m_comm_queues.issue_to_execute);
    commit->set_execute_commit_q(m_comm_queues.execute_to_commit);
}

