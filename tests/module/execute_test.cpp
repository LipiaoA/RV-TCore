#include <cassert>
#include <cstdlib>
#include <vector>
#include <memory>
#include "log.h"

#include "execute/execute_unit.h"
#include "execute/lsu/lsu.h"
#include "icache/dcache.h"
#include "memory/cache.h"
#include "memory/tlb.h"
#include "rob/rob.h"
#include "tools/queue.h"
#include "util/util.h"
#include "isa/opcode.h"

// 当前 ExecuteUnit::tick/commit 不消费 issue_execute、不写 execute_commit（空实现）。
// 本测例验证时序约束：在 issue_execute 读侧注入 MicroOp 后，多周期 tick 后下游队列仍无输出。

int main()
{
    using namespace std;
    auto &log = LogSystem::instance();
    log.set_level(LogLevel::LEVEL3);
    log.add_writer(make_shared<ConsoleSink>());
    log.set_mode(LogMode::SYNC);
    LOG_LEVEL1("ExecuteTest", "Starting in synchronous mode");
    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels = {{1}};
    MultiLevelTLB tlb(&sched, 12, levels);

    CacheConfig l2cfg(static_cast<size_t>(512 * 1024), 64, 8, 4);
    l2cfg.inclusion_policy = InclusionPolicy::Inclusive;
    DCache l2(&sched, l2cfg);
    l2.set_name("l2");

    CacheConfig dcfg(static_cast<size_t>(64 * 1024), 64, 4, 4);
    dcfg.inclusion_policy = InclusionPolicy::NonInclusive;
    DCache dcache(&sched, dcfg);
    dcache.attach_lower(&l2);
    dcache.set_name("dcache");

    ASyncQueue<ExecResult> execute_commit_q(64);
    ASyncQueue<MicroOp> lsu_rename_issue_q(8);
    ASyncQueue<ExecResult> lsu_exec_stub(8);
    ROBDeps rob_deps {};
    rob_deps.execute_commit_q = &execute_commit_q;
    ROBConfig rob_conf {};
    rob_conf.capacity = 16;
    ROB rob(rob_conf, rob_deps);

    LSUDeps lsu_deps {};
    lsu_deps.rob = &rob;
    lsu_deps.dcache = &dcache;
    lsu_deps.dtlb = &tlb;
    lsu_deps.rename_issue_q = &lsu_rename_issue_q;
    lsu_deps.execute_commit_q = &lsu_exec_stub;

    LSUConfig lsu_conf {};
    LSU lsu(lsu_conf, lsu_deps);
    lsu.build();

    ASyncQueue<MicroOp> issue_execute_q(32);
    ASyncQueue<ExecResult> exec_to_commit_q(32);

    ExecDeps exec_deps {};
    exec_deps.issue_execute_q = &issue_execute_q;
    exec_deps.execute_commit_q = &exec_to_commit_q;
    exec_deps.lsu = &lsu;

    ExecConfig exec_conf {};
    exec_conf.alu_num = 1;
    exec_conf.fpu_num = 0;
    exec_conf.bru_num = 0;

    ExecuteUnit execute(exec_conf, exec_deps);
    execute.build();

    MicroOp uop {};
    uop.valid = true;
    uop.opcode = OP_ADDI;
    assert(issue_execute_q.write(uop));
    issue_execute_q.tick();
    assert(issue_execute_q.size() == 1);

    for (int c = 0; c < 4; ++c) {
        execute.tick();
        assert(exec_to_commit_q.empty());
        exec_to_commit_q.tick();
        assert(exec_to_commit_q.empty());
    }

    execute.commit();
    assert(exec_to_commit_q.empty());
    assert(issue_execute_q.size() == 1);

    return EXIT_SUCCESS;
}
