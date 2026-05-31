#include <cassert>
#include <cstdlib>

#include "rename/rename_unit.h"
#include "regfile_unit.h"
#include "rob/rob.h"
#include "module_comm.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// RenameUnit 单周期内顺序为 r2 → r1 → r0（级联流水寄存器）：
//   r0：从 decode_rename 读侧取指进入 r1_buf
//   r1：分配物理寄存器与 ROB，进入 r2_buf
//   r2：dispatch 到 integer / ls / fp 队列写侧
// 与 ASyncQueue 组合：每步 rename.tick() 前后可插入 decode_rename.tick() / dispatch.tick() 观测读侧

int main()
{
    ASyncQueue<MInstPtr> decode_rename_q(32);
    ASyncQueue<MInstPtr> dispatch_issue_q(32);
    ASyncQueue<RegReq> reg_req_q(32);
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> alu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bru_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> lsu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> fpu_resp_q;
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

    std::array<int, RV_GPR_SIZE> crat {};
    crat.fill(0);
    crat[2] = 2;
    std::vector<RatCheckpoint> checkpoints;

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.rename_issue_q = &dispatch_issue_q;
    rename_deps.regfile = &regfile;
    rename_deps.flush_event = &flush_event;
    rename_deps.crat = &crat;
    rename_deps.checkpoints = &checkpoints;

    RenameConfig rename_conf {};
    rename_conf.width = 4;
    rename_conf.prf_capacity = 64;
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    MInstPtr uop = new MicroOp();
    uop->valid = true;
    uop->opcode = OP_ADDI;
    uop->raw_inst = 0x00110193u;
    uop->pc = 0x2000;
    uop->rd.valid = true;
    uop->rd.atag = 3;
    uop->rs1.valid = true;
    uop->rs1.atag = 2;
    uop->rs2.valid = true;
    uop->rs2.atag = 0;

    // 周期 0：decode_rename 写侧注入 → tick 后读侧 1 条；dispatch 仍空
    assert(decode_rename_q.write(uop));
    decode_rename_q.tick();
    assert(decode_rename_q.size() == 1);
    assert(dispatch_issue_q.empty());

    // 周期 1：rename.tick #1 — r0 取指，decode_rename 读侧被读空；尚未到 dispatch
    rename.tick();
    assert(decode_rename_q.empty());
    assert(dispatch_issue_q.empty());

    // 周期 2：rename.tick #2 — r1 分配 ROB/物理寄存器，dispatch 仍空
    rename.tick();
    assert(dispatch_issue_q.empty());

    // 周期 3：rename.tick #3 — r2 dispatch 写入 dispatch 写侧；读侧仍空
    rename.tick();
    assert(dispatch_issue_q.empty());

    dispatch_issue_q.tick();
    assert(dispatch_issue_q.size() == 1);

    MInstPtr out {};
    assert(dispatch_issue_q.read(out));
    assert(out->valid);
    assert(out->opcode == OP_ADDI);
    assert(out->rs1.ptag == 2);
    assert(out->rs1.ready);
    assert(out->rd.ptag != 2);

    return EXIT_SUCCESS;
}
