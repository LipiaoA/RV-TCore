#include <cassert>
#include <cstdlib>

#include "rename/rename_unit.h"
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
    ASyncQueue<ExecResult> inst_commit_q(64);
    ASyncQueue<MicroOp> decode_rename_q(32);
    ASyncQueue<MicroOp> dispatch_issue_q(32);

    ROBConfig rob_conf {};
    rob_conf.capacity = 8;
    ROBDeps rob_deps {};
    rob_deps.inst_commit_q = &inst_commit_q;
    ROB rob(rob_conf, rob_deps);

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.rename_issue_q = &dispatch_issue_q;

    RenameConfig rename_conf {};
    rename_conf.width = 4;
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    MicroOp uop {};
    uop.valid = true;
    uop.opcode = OP_ADDI;
    uop.raw_inst = 0x00100093u;
    uop.pc = 0x2000;
    uop.rd.valid = true;
    uop.rd.atag = 1;
    uop.rs1.valid = true;
    uop.rs1.atag = 0;
    uop.rs2.valid = true;
    uop.rs2.atag = 0;

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

    MicroOp out {};
    assert(dispatch_issue_q.read(out));
    assert(out.valid);
    assert(out.opcode == OP_ADDI);
    assert(out.rd.ptag >= RV_GPR_SIZE);

    return EXIT_SUCCESS;
}
