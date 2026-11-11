#include <cassert>
#include <cstdlib>

#include "rename/rename_unit.h"
#include "regfile_unit.h"
#include "vregfile_unit.h"
#include "tregfile_unit.h"
#include "execute/lsu/lsu.h"
#include "rob/rob.h"
#include "module_comm.h"
#include "perf/perf_def.h"
#include "tools/queue.h"
#include "isa/opcode.h"
#include "icache/dcache.h"
#include "memory/tlb.h"

int main()
{
    PerfStats stats {};
    ASyncQueue<MInstPtr> decode_rename_q(32);
    ASyncQueue<MInstPtr> dispatch_issue_q(32);
    ASyncQueue<RegReq> reg_req_q(32);
    ASyncQueue<VecRegReq> vec_reg_req_q(32);
    ASyncQueue<TileRegReq> tile_reg_req_q(32);
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> alu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bru_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> lsu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> fpu_resp_q;
    FixedArray<VecRegResp, MAX_REGISTER_REQ_CAPACITY> vpu_resp_q;
    FixedArray<TileRegResp, MAX_REGISTER_REQ_CAPACITY> tpu_resp_q;
    std::vector<WakeupEvent> wakeup_bus;
    FlushEvent flush_event {};

    ROBConfig rob_conf {};
    rob_conf.capacity = 8;
    ROBDeps rob_deps {};
    rob_deps.flush_event = &flush_event;
    ROB rob(rob_conf, rob_deps);

    RegFileDeps regfile_deps {};
    regfile_deps.reg_req_q = &reg_req_q;
    regfile_deps.alu_resp_q = &alu_resp_q;
    regfile_deps.bru_resp_q = &bru_resp_q;
    regfile_deps.lsu_resp_q = &lsu_resp_q;
    regfile_deps.fpu_resp_q = &fpu_resp_q;
    regfile_deps.wakeup_bus = &wakeup_bus;
    regfile_deps.flush_event = &flush_event;
    RegFileConfig regfile_conf {};
    regfile_conf.capacity = 64;
    RegFileUnit regfile(regfile_conf, regfile_deps);
    constexpr u64 stack_pointer = 0x80000000;
    regfile.init_phys(2, stack_pointer);

    VRegFileDeps vregfile_deps {};
    vregfile_deps.vec_reg_req_q = &vec_reg_req_q;
    vregfile_deps.vpu_resp_q = &vpu_resp_q;
    vregfile_deps.wakeup_bus = &wakeup_bus;
    vregfile_deps.flush_event = &flush_event;
    VRegFileConfig vregfile_conf {};
    vregfile_conf.capacity = 64;
    VRegFileUnit vregfile(vregfile_conf, vregfile_deps);

    TRegFileDeps tregfile_deps {};
    tregfile_deps.tile_reg_req_q = &tile_reg_req_q;
    tregfile_deps.tpu_resp_q = &tpu_resp_q;
    tregfile_deps.wakeup_bus = &wakeup_bus;
    tregfile_deps.flush_event = &flush_event;
    TRegFileConfig tregfile_conf {};
    tregfile_conf.capacity = 16;
    TRegFileUnit tregfile(tregfile_conf, tregfile_deps);

    std::array<int, RV_GPR_SIZE> crat {};
    crat.fill(0);
    crat[2] = 2;
    std::array<int, RV_VREG_SIZE> vcrat {};
    for (u32 i = 0; i < RV_VREG_SIZE; ++i) {
        vcrat[i] = static_cast<int>(i);
    }
    std::array<int, RV_TILE_SIZE> tcrat {};
    for (u32 i = 0; i < RV_TILE_SIZE; ++i) {
        tcrat[i] = static_cast<int>(i);
    }
    std::vector<RatCheckpoint> checkpoints;

    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels = {{1}};
    MultiLevelTLB tlb(&sched, 12, levels);
    DCache l2(&sched, CacheConfig(static_cast<size_t>(64 * 1024), 64, 4, 1));
    DCache dcache(&sched, CacheConfig(static_cast<size_t>(16 * 1024), 64, 2, 1));
    dcache.attach_lower(&l2);
    std::vector<LQEntry> lsu_ld_q;
    std::vector<SQEntry> lsu_st_q;
    LSUDeps lsu_deps {};
    lsu_deps.rob = &rob;
    lsu_deps.dcache = &dcache;
    lsu_deps.dtlb = &tlb;
    lsu_deps.sched = &sched;
    lsu_deps.lsu_ld_q = &lsu_ld_q;
    lsu_deps.lsu_st_q = &lsu_st_q;
    lsu_deps.perf_stats = &stats;
    LSUConfig lsu_conf {};
    lsu_conf.load_q_size = 8;
    lsu_conf.store_q_size = 8;
    LSU lsu(lsu_conf, lsu_deps);
    lsu.build();

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.rename_issue_q = &dispatch_issue_q;
    rename_deps.regfile = &regfile;
    rename_deps.vregfile = &vregfile;
    rename_deps.tregfile = &tregfile;
    rename_deps.lsu = &lsu;
    rename_deps.flush_event = &flush_event;
    rename_deps.crat = &crat;
    rename_deps.vcrat = &vcrat;
    rename_deps.tcrat = &tcrat;
    rename_deps.checkpoints = &checkpoints;
    rename_deps.perf_stats = &stats;

    RenameConfig rename_conf {};
    rename_conf.width = 4;
    rename_conf.prf_capacity = 64;
    rename_conf.vprf_capacity = 64;
    rename_conf.tprf_capacity = 16;
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    MInstPtr uop = new MicroOp();
    uop->valid = true;
    uop->opcode = OP_ADDI;
    uop->type = InstType::INT;
    uop->raw_inst = 0x00110193u;
    uop->pc = 0x2000;
    uop->rd.valid = true;
    uop->rd.atag = 3;
    uop->rs1.valid = true;
    uop->rs1.atag = 2;
    uop->rs2.valid = true;
    uop->rs2.atag = 0;

    assert(decode_rename_q.write(uop));
    decode_rename_q.tick();
    assert(decode_rename_q.rsize() == 1);
    assert(dispatch_issue_q.empty());

    rename.tick();
    assert(decode_rename_q.empty());
    assert(dispatch_issue_q.empty());

    rename.tick();
    assert(dispatch_issue_q.empty());

    rename.tick();
    assert(dispatch_issue_q.empty());

    dispatch_issue_q.tick();
    assert(dispatch_issue_q.rsize() == 1);

    MInstPtr out {};
    assert(dispatch_issue_q.read(out));
    assert(out->valid);
    assert(out->opcode == OP_ADDI);
    assert(out->rs1.ptag == 2);
    assert(out->rs1.ready);
    assert(out->rd.ptag != 2);

    return EXIT_SUCCESS;
}
