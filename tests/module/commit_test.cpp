#include <cassert>
#include <cstdlib>

#include "commit/commit_unit.h"
#include "rename/rename_unit.h"
#include "rob/rob.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// CommitUnit::tick() 当前无组合逻辑；顺序提交在 commit() 中完成。
// 时序：ROB 头项需 ready 后，commit.commit() 才弹出；tick() 可显式调用以匹配其它单元接口。

int main()
{
    ASyncQueue<ExecResult> execute_commit_q(64);
    ASyncQueue<MicroOp> decode_rename_q(64);
    ASyncQueue<MicroOp> dispatch_issue_q(64);

    ROBConfig rob_conf {};
    rob_conf.capacity = 8;
    ROBDeps rob_deps {};
    rob_deps.execute_commit_q = &execute_commit_q;
    ROB rob(rob_conf, rob_deps);

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.integer_issue_q = &dispatch_issue_q;
    rename_deps.ls_issue_q = &dispatch_issue_q;
    rename_deps.fp_issue_q = &dispatch_issue_q;
    RenameConfig rename_conf {};
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    CommitDeps commit_deps {};
    commit_deps.execute_commit_q = &execute_commit_q;
    commit_deps.rob = &rob;
    commit_deps.rename = &rename;
    CommitConfig commit_conf {};
    CommitUnit commit(commit_conf, commit_deps);
    commit.build();

    MicroOp uop {};
    uop.valid = true;
    uop.opcode = OP_ADDI;
    ROBID rid = rob.alloc(uop);
    assert(rid.idx < rob_conf.capacity);
    assert(rob.size() == 1);

    // 周期 0：头项未 ready，commit 无法弹出
    commit.tick();
    commit.commit();
    assert(rob.size() == 1);

    // 周期 1：mark_ready 后 commit.commit 弹出 ROB 头
    rob.mark_ready(rid, 42);
    commit.tick();
    commit.commit();
    assert(rob.empty());

    return EXIT_SUCCESS;
}
