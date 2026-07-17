#include <cassert>
#include <cstdlib>
#include <vector>

#include "issue/issue_unit.h"
#include "execute/execute_unit.h"
#include "execute/lsu/lsu.h"
#include "icache/dcache.h"
#include "memory/tlb.h"
#include "rob/rob.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// Issue select consumes wakeup events before picking ready instructions and
// sends PRF read requests for selected source operands in the same cycle.

int main()
{
    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels = {{1}};
    MultiLevelTLB tlb(&sched, 12, levels);

    CacheConfig l2cfg(static_cast<size_t>(512 * 1024), 64, 8, 4);
    DCache l2(&sched, l2cfg);
    CacheConfig dcfg(static_cast<size_t>(64 * 1024), 64, 4, 4);
    DCache dcache(&sched, dcfg);
    dcache.attach_lower(&l2);
    Memory mem;
    mem.add_segment(0x1000, 0x2000);
    l2.set_mem(&mem);

    ASyncQueue<MInstPtr> rename_issue_q(32);
    ASyncQueue<RegReq> reg_req_q(32);
    ASyncQueue<VecRegReq> vec_reg_req_q(32);
    ASyncQueue<TileRegReq> tile_reg_req_q(32);
    ASyncQueue<FuAcceptEvent> fu_accept_q(32);
    ASyncQueue<MInstPtr> inst_commit_q(32);
    ASyncQueue<FlushEvent> flush_req_q(32);
    FlushEvent flush_event {};
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> lsu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> alu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bru_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> fpu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bypass_q;
    std::vector<LQEntry> load_q;
    std::vector<SQEntry> store_q;
    std::vector<WakeupEvent> wakeup_bus;

    ROBDeps rob_deps {};
    rob_deps.flush_event = &flush_event;
    ROBConfig rob_conf {};
    rob_conf.capacity = 16;
    ROB rob(rob_conf, rob_deps);

    LSUDeps lsu_deps {};
    lsu_deps.rob = &rob;
    lsu_deps.dcache = &dcache;
    lsu_deps.dtlb = &tlb;
    lsu_deps.mem = &mem;
    lsu_deps.sched = &sched;
    lsu_deps.reg_req_q = &reg_req_q;
    lsu_deps.reg_resp_q = &lsu_resp_q;
    lsu_deps.reg_bypass_bus = &bypass_q;
    lsu_deps.flush_req_q = &flush_req_q;
    lsu_deps.wakeup_bus = &wakeup_bus;
    lsu_deps.lsu_ld_q = &load_q;
    lsu_deps.lsu_st_q = &store_q;
    LSUConfig lsu_conf {};
    LSU lsu(lsu_conf, lsu_deps);

    ExecDeps exec_deps {};
    exec_deps.lsu = &lsu;
    exec_deps.inst_commit_q = &inst_commit_q;
    exec_deps.flush_event = &flush_event;
    exec_deps.rob = &rob;
    ALUDeps alu_deps {};
    alu_deps.rob = &rob;
    alu_deps.reg_req_q = &reg_req_q;
    alu_deps.reg_resp_q = &alu_resp_q;
    alu_deps.reg_bypass_bus = &bypass_q;
    alu_deps.wakeup_bus = &wakeup_bus;
    BRUDeps bru_deps {};
    bru_deps.rob = &rob;
    bru_deps.reg_req_q = &reg_req_q;
    bru_deps.flush_req_q = &flush_req_q;
    bru_deps.reg_resp_q = &bru_resp_q;
    bru_deps.reg_bypass_bus = &bypass_q;
    bru_deps.wakeup_bus = &wakeup_bus;
    FPUDeps fpu_deps {};
    fpu_deps.reg_req_q = &reg_req_q;
    fpu_deps.reg_resp_q = &fpu_resp_q;
    fpu_deps.reg_bypass_bus = &bypass_q;
    fpu_deps.flush_event = &flush_event;
    fpu_deps.rob = &rob;
    fpu_deps.wakeup_bus = &wakeup_bus;
    exec_deps.alu_deps = &alu_deps;
    exec_deps.bru_deps = &bru_deps;
    exec_deps.fpu_deps = &fpu_deps;
    exec_deps.lsu_deps = &lsu_deps;
    ExecConfig exec_conf {};
    exec_conf.alu_num = 0;
    exec_conf.bru_num = 0;
    exec_conf.fpu_num = 0;
    exec_conf.agu_num = 0;
    ExecuteUnit execute(exec_conf, exec_deps);

    IssueDeps deps {};
    deps.exec = &execute;
    deps.lsu = &lsu;
    deps.rob = &rob;
    deps.rename_issue_q = &rename_issue_q;
    deps.reg_req_q = &reg_req_q;
    deps.vec_reg_req_q = &vec_reg_req_q;
    deps.tile_reg_req_q = &tile_reg_req_q;
    deps.fu_accept_q = &fu_accept_q;
    deps.wakeup_bus = &wakeup_bus;
    deps.flush_event = &flush_event;

    IssueConfig conf {};
    conf.int_issue_width = 1;
    conf.ls_issue_width = 0;
    conf.fp_issue_width = 0;

    IssueUnit issue(conf, deps);
    issue.build();

    MInstPtr inst = new MicroOp();
    inst->valid = true;
    inst->opcode = OP_ADD;
    inst->type = InstType::INT;
    inst->rs1.valid = true;
    inst->rs1.ptag = 5;
    inst->rs1.ready = false;
    inst->rs2.valid = false;
    inst->rd.valid = true;
    inst->rd.ptag = 9;
    inst->rid = ROBID{0, 0};

    WakeupEvent event {};
    event.valid = true;
    event.type = WAKEUP_BYPASS;
    event.ptag = inst->rs1.ptag;
    event.bypassable = true;
    wakeup_bus.push_back(event);

    assert(rename_issue_q.write(inst));
    assert(rename_issue_q.empty());
    rename_issue_q.tick();
    assert(rename_issue_q.size() == 1);

    issue.tick();
    assert(rename_issue_q.empty());
    assert(wakeup_bus.empty());

    issue.commit();
    RegReq req {};
    assert(reg_req_q.empty());

    // FU busy: pending dispatch must not re-issue PRF reads on the next select.
    IssueConfig busy_conf = conf;
    busy_conf.int_issue_width = 2;
    exec_conf.alu_num = 1;
    exec_conf.alu_conf.stages = 2;
    ExecuteUnit execute_busy(exec_conf, exec_deps);
    deps.exec = &execute_busy;
    IssueUnit issue_busy(busy_conf, deps);
    issue_busy.build();

    auto make_ready_int = [&](u32 rob_idx, u32 ptag_rs1) {
        MInstPtr uop = new MicroOp();
        uop->valid = true;
        uop->opcode = OP_ADD;
        uop->type = InstType::INT;
        uop->rs1.valid = true;
        uop->rs1.ptag = ptag_rs1;
        uop->rs1.ready = true;
        uop->rs2.valid = false;
        uop->rd.valid = true;
        uop->rd.ptag = ptag_rs1 + 10;
        uop->rid = ROBID{rob_idx, 0};
        return uop;
    };

    MInstPtr inst_a = make_ready_int(0, 1);
    MInstPtr inst_b = make_ready_int(1, 2);
    assert(rename_issue_q.write(inst_a));
    assert(rename_issue_q.write(inst_b));
    rename_issue_q.tick();

    issue_busy.tick();
    issue_busy.commit();
    issue_busy.tick();
    issue_busy.commit();
    assert(reg_req_q.read(req));
    assert(reg_req_q.empty());
    assert(inst_a->fu_pending);
    assert(inst_a->state == InstState::RENAMED);

    issue_busy.tick();
    issue_busy.commit();
    assert(reg_req_q.empty());

    IssueConfig fp_conf {};
    fp_conf.int_issue_width = 0;
    fp_conf.ls_issue_width = 0;
    fp_conf.fp_issue_width = 1;
    ExecConfig fp_exec_conf = exec_conf;
    fp_exec_conf.alu_num = 0;
    fp_exec_conf.fpu_num = 1;
    fp_exec_conf.fpu_conf.stages = 2;
    ExecuteUnit execute_fp(fp_exec_conf, exec_deps);
    deps.exec = &execute_fp;
    IssueUnit issue_fp(fp_conf, deps);
    issue_fp.build();

    MInstPtr fp_inst = new MicroOp();
    fp_inst->valid = true;
    fp_inst->opcode = OP_FADD_S;
    fp_inst->type = InstType::FP;
    fp_inst->rs1.valid = true;
    fp_inst->rs1.ptag = 31;
    fp_inst->rs1.ready = true;
    fp_inst->rs2.valid = false;
    fp_inst->rid = ROBID{2, 0};
    assert(rename_issue_q.write(fp_inst));
    rename_issue_q.tick();

    issue_fp.tick();
    issue_fp.commit();
    issue_fp.tick();
    issue_fp.commit();
    assert(reg_req_q.read(req));
    assert(req.valid);
    assert(!req.write);
    assert(req.ptag == fp_inst->rs1.ptag);
    assert(req.fu == FuType::FPU);
    assert(reg_req_q.empty());

    AGUDeps agu_deps {};
    agu_deps.lsu_st_q = &store_q;
    agu_deps.lsu_ld_q = &load_q;
    agu_deps.reg_req_q = &reg_req_q;
    agu_deps.reg_resp_q = &lsu_resp_q;
    agu_deps.reg_bypass_bus = &bypass_q;
    agu_deps.fu_accept_q = &fu_accept_q;
    agu_deps.rob = &rob;
    agu_deps.lsu = &lsu;

    STDDeps std_deps {};
    std_deps.lsu_st_q = &store_q;
    std_deps.lsu_ld_q = &load_q;
    std_deps.reg_req_q = &reg_req_q;
    std_deps.reg_resp_q = &lsu_resp_q;
    std_deps.reg_bypass_bus = &bypass_q;
    std_deps.fu_accept_q = &fu_accept_q;
    std_deps.rob = &rob;
    std_deps.lsu = &lsu;

    ExecConfig store_exec_conf {};
    store_exec_conf.alu_num = 0;
    store_exec_conf.bru_num = 0;
    store_exec_conf.fpu_num = 0;
    store_exec_conf.agu_num = 1;
    store_exec_conf.std_num = 1;
    store_exec_conf.agu_conf.stages = 2;
    store_exec_conf.std_conf.stages = 2;
    exec_deps.agu_deps = &agu_deps;
    exec_deps.std_deps = &std_deps;
    ExecuteUnit store_execute(store_exec_conf, exec_deps);

    IssueConfig store_conf {};
    store_conf.int_issue_width = 0;
    store_conf.ls_issue_width = 2;
    deps.exec = &store_execute;
    IssueUnit store_issue(store_conf, deps);

    MInstPtr store = new MicroOp();
    store->valid = true;
    store->state = InstState::RENAMED;
    store->opcode = OP_SD;
    store->type = InstType::STORE;
    store->rs1.valid = true;
    store->rs1.ready = true;
    store->rs1.ptag = 41;
    store->rs2.valid = true;
    store->rs2.ready = true;
    store->rs2.ptag = 42;
    store->rid = ROBID{3, 0};

    assert(rename_issue_q.write(store));
    rename_issue_q.tick();
    store_issue.tick();
    store_issue.tick();
    store_issue.commit();
    assert(store->sta_state == StorePartState::WAIT_ACCEPT);
    assert(store->std_state == StorePartState::WAIT_ACCEPT);
    assert(store->state == InstState::RENAMED);
    assert(reg_req_q.read(req));
    assert(req.ptag == store->rs1.ptag);
    assert(req.fu == FuType::AGU);
    assert(reg_req_q.read(req));
    assert(req.ptag == store->rs2.ptag);
    assert(req.fu == FuType::STD);
    assert(reg_req_q.empty());

    FuAcceptEvent sta_accept {};
    sta_accept.valid = true;
    sta_accept.inst = store;
    sta_accept.part = StorePart::STA;
    sta_accept.result = FuAcceptResult::ACCEPTED;
    sta_accept.generation = store->sta_generation;
    assert(fu_accept_q.write(sta_accept));
    fu_accept_q.tick();
    store_issue.tick();
    assert(store->sta_state == StorePartState::ACCEPTED);
    assert(store->std_state == StorePartState::WAIT_ACCEPT);
    assert(store->state != InstState::FU_ACTIVE);

    FuAcceptEvent std_accept = sta_accept;
    std_accept.part = StorePart::STD;
    std_accept.generation = store->std_generation;
    assert(fu_accept_q.write(std_accept));
    fu_accept_q.tick();
    store_issue.tick();
    assert(store->sta_state == StorePartState::ACCEPTED);
    assert(store->std_state == StorePartState::ACCEPTED);
    assert(store->state == InstState::FU_ACTIVE);

    FuAcceptEvent std_replay = std_accept;
    std_replay.result = FuAcceptResult::REPLAY;
    assert(fu_accept_q.write(std_replay));
    fu_accept_q.tick();
    store_issue.tick();
    assert(store->sta_state == StorePartState::ACCEPTED);
    assert(store->std_state == StorePartState::WAIT_ISSUE);
    assert(store->rs2.ready);
    assert(store->state == InstState::REPLAY);

    assert(fu_accept_q.write(std_accept));
    fu_accept_q.tick();
    store_issue.tick();
    assert(store->std_state == StorePartState::WAIT_ISSUE);
    assert(store->state == InstState::REPLAY);

    return EXIT_SUCCESS;
}
