#include <cassert>
#include <cstdlib>
#include <vector>

#include "execute/alu.h"
#include "execute/bru.h"
#include "execute/execute_unit.h"
#include "execute/lsu/lsu.h"
#include "icache/dcache.h"
#include "memory/tlb.h"
#include "regfile_unit.h"
#include "rob/rob.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// Execute-side wakeup sources: ALU/BRU announce bypass readiness in R1, and
// RegFile announces writeback readiness when a physical register is written.

int main()
{
    ASyncQueue<RegReq> reg_req_q(32);
    ASyncQueue<FuAcceptEvent> fu_accept_q(32);
    ASyncQueue<FlushEvent> flush_req_q(8);
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> alu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bru_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> lsu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> fpu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bypass_q;
    FlushEvent flush_event {};
    std::vector<WakeupEvent> wakeup_bus;

    ROBDeps rob_deps {};
    rob_deps.flush_event = &flush_event;
    ROBConfig rob_conf {};
    rob_conf.capacity = 16;
    ROB rob(rob_conf, rob_deps);

    ALUDeps alu_deps {};
    alu_deps.rob = &rob;
    alu_deps.reg_req_q = &reg_req_q;
    alu_deps.reg_resp_q = &alu_resp_q;
    alu_deps.reg_bypass_bus = &bypass_q;
    alu_deps.wakeup_bus = &wakeup_bus;
    ALUConfig alu_conf {};
    alu_conf.stages = 2;
    ALU alu(alu_conf, alu_deps);

    MInstPtr alu_inst = new MicroOp();
    alu_inst->valid = true;
    alu_inst->opcode = OP_ADDI;
    alu_inst->type = InstType::INT;
    alu_inst->rd.valid = true;
    alu_inst->rd.ptag = 11;
    assert(alu.dispatch(alu_inst));
    alu.commit();
    alu.tick();
    assert(alu_inst->state == InstState::FU_ACTIVE);
    assert(wakeup_bus.size() == 1);
    assert(wakeup_bus[0].valid);
    assert(wakeup_bus[0].type == WAKEUP_BYPASS);
    assert(wakeup_bus[0].ptag == alu_inst->rd.ptag);
    wakeup_bus.clear();

    ALU replay_alu(alu_conf, alu_deps);
    MInstPtr replay_inst = new MicroOp();
    replay_inst->valid = true;
    replay_inst->state = InstState::RENAMED;
    replay_inst->fu_pending = true;
    replay_inst->opcode = OP_ADD;
    replay_inst->type = InstType::INT;
    replay_inst->rs1.valid = true;
    replay_inst->rs1.ptag = 20;
    assert(replay_alu.dispatch(replay_inst));
    replay_alu.commit();
    replay_alu.tick();
    assert(replay_inst->state == InstState::REPLAY);
    assert(!replay_inst->fu_pending);

    BRUDeps bru_deps {};
    bru_deps.rob = &rob;
    bru_deps.reg_req_q = &reg_req_q;
    bru_deps.flush_req_q = &flush_req_q;
    bru_deps.reg_resp_q = &bru_resp_q;
    bru_deps.reg_bypass_bus = &bypass_q;
    bru_deps.wakeup_bus = &wakeup_bus;
    BRUConfig bru_conf {};
    bru_conf.stages = 2;
    BRU bru(bru_conf, bru_deps);

    MInstPtr bru_inst = new MicroOp();
    bru_inst->valid = true;
    bru_inst->opcode = OP_JAL;
    bru_inst->type = InstType::BRANCH;
    bru_inst->rd.valid = true;
    bru_inst->rd.ptag = 12;
    assert(bru.dispatch(bru_inst));
    bru.commit();
    bru.tick();
    assert(bru_inst->state == InstState::FU_ACTIVE);
    assert(wakeup_bus.size() == 1);
    assert(wakeup_bus[0].valid);
    assert(wakeup_bus[0].type == WAKEUP_BYPASS);
    assert(wakeup_bus[0].ptag == bru_inst->rd.ptag);
    wakeup_bus.clear();

    RegFileDeps regfile_deps {};
    regfile_deps.reg_req_q = &reg_req_q;
    regfile_deps.alu_resp_q = &alu_resp_q;
    regfile_deps.bru_resp_q = &bru_resp_q;
    regfile_deps.lsu_resp_q = &lsu_resp_q;
    regfile_deps.fpu_resp_q = &fpu_resp_q;
    regfile_deps.flush_event = &flush_event;
    regfile_deps.wakeup_bus = &wakeup_bus;
    RegFileConfig regfile_conf {};
    regfile_conf.capacity = 64;
    RegFileUnit regfile(regfile_conf, regfile_deps);

    RegReq write_req {};
    write_req.valid = true;
    write_req.write = true;
    write_req.ptag = 13;
    write_req.val = 0x1234;
    assert(reg_req_q.write(write_req));
    reg_req_q.commit();
    regfile.tick();
    assert(wakeup_bus.size() == 1);
    assert(wakeup_bus[0].valid);
    assert(wakeup_bus[0].type == WAKEUP_WRITEBACK);
    assert(wakeup_bus[0].ptag == write_req.ptag);

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
    std::vector<LQEntry> load_q;
    std::vector<SQEntry> store_q;

    ASyncQueue<MInstPtr> inst_commit_q(32);
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
    lsu_conf.dtlb_hit_latency = 1;
    lsu_conf.dcache_line_size = 64;
    LSU lsu(lsu_conf, lsu_deps);
    lsu.build();

    AGUDeps agu_deps {};
    agu_deps.rob = &rob;
    agu_deps.reg_req_q = &reg_req_q;
    agu_deps.reg_resp_q = &lsu_resp_q;
    agu_deps.reg_bypass_bus = &bypass_q;
    agu_deps.fu_accept_q = &fu_accept_q;
    agu_deps.lsu_ld_q = &load_q;
    agu_deps.lsu_st_q = &store_q;
    agu_deps.lsu = &lsu;
    STDDeps std_deps {};
    std_deps.rob = &rob;
    std_deps.reg_req_q = &reg_req_q;
    std_deps.reg_resp_q = &lsu_resp_q;
    std_deps.reg_bypass_bus = &bypass_q;
    std_deps.fu_accept_q = &fu_accept_q;
    std_deps.lsu_ld_q = &load_q;
    std_deps.lsu_st_q = &store_q;
    std_deps.lsu = &lsu;

    ExecDeps exec_deps {};
    exec_deps.lsu = &lsu;
    exec_deps.inst_commit_q = &inst_commit_q;
    exec_deps.flush_event = &flush_event;
    exec_deps.rob = &rob;
    exec_deps.alu_deps = &alu_deps;
    exec_deps.bru_deps = &bru_deps;
    exec_deps.agu_deps = &agu_deps;
    exec_deps.std_deps = &std_deps;
    FPUDeps fpu_deps {};
    fpu_deps.reg_req_q = &reg_req_q;
    fpu_deps.reg_resp_q = &fpu_resp_q;
    fpu_deps.reg_bypass_bus = &bypass_q;
    fpu_deps.flush_event = &flush_event;
    fpu_deps.rob = &rob;
    fpu_deps.wakeup_bus = &wakeup_bus;
    exec_deps.fpu_deps = &fpu_deps;

    ExecConfig exec_conf {};
    exec_conf.fpu_num = 1;
    exec_conf.agu_num = 1;
    exec_conf.std_num = 1;
    exec_conf.commit_width = 2;
    exec_conf.fpu_conf.stages = 2;
    exec_conf.agu_conf.stages = 2;
    exec_conf.std_conf.stages = 2;
    exec_conf.lsu_conf = lsu_conf;
    ExecuteUnit execute(exec_conf, exec_deps);
    execute.build();

    MInstPtr fp_inst = new MicroOp();
    fp_inst->valid = true;
    fp_inst->opcode = OP_FADD_S;
    fp_inst->type = InstType::FP;
    fp_inst->rid = ROBID{1, 0};
    assert(execute.dispatch(fp_inst));
    execute.commit();
    execute.tick();
    execute.tick();
    execute.commit();
    MInstPtr committed_inst = nullptr;
    assert(inst_commit_q.read(committed_inst));
    assert(committed_inst == fp_inst);

    MInstPtr load_inst = new MicroOp();
    load_inst->valid = true;
    load_inst->opcode = OP_LD;
    load_inst->type = InstType::LOAD;
    load_inst->rs1.valid = false;
    load_inst->imm = 0x1000;
    load_inst->rd.valid = true;
    load_inst->rd.ptag = 21;
    load_inst->rid = ROBID{2, 0};
    assert(lsu.allocate(load_inst));
    assert(execute.dispatch(load_inst));
    for (int i = 0; i < 32; ++i) {
        execute.commit();
        execute.tick();
        sched.tick();
    }
    execute.commit();
    committed_inst = nullptr;
    assert(inst_commit_q.read(committed_inst));
    assert(committed_inst == load_inst);
    assert(wakeup_bus.size() >= 1);

    return EXIT_SUCCESS;
}
