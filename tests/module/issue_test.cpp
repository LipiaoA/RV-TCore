#include <cassert>
#include <cstdlib>

#include "issue/issue_unit.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// IssueUnit 单周期内：先读尽 dispatch_issue 读侧入内部队列，再向 issue_execute 写侧发射。
// 与 ASyncQueue 组合时的步进：
//   ① dispatch_issue.tick()：使注入的 MicroOp 出现在读侧
//   ② issue.tick()：读 dispatch、写 issue_execute 写侧
//   ③ issue_execute.tick()：发射结果出现在读侧，可 read 验证

int main()
{
    ASyncQueue<MicroOp> rename_issue_q(32);

    IssueDeps deps {};
    deps.rename_issue_q = &rename_issue_q;

    IssueConfig conf {};
    conf.int_issue_width = 4;
    conf.ls_issue_width = 2;
    conf.fp_issue_width = 2;

    IssueUnit issue(conf, deps);
    issue.build();

    MicroOp uop {};
    uop.valid = true;
    uop.opcode = OP_ADDI;
    uop.rs1.ready = true;
    uop.rs2.ready = true;
    uop.issued = false;

    // 周期 0：dispatch 写侧注入 → tick 后读侧可见；issue.tick 发射到 issue_execute 写侧（读侧仍空）
    assert(rename_issue_q.write(uop));
    assert(rename_issue_q.empty());
    rename_issue_q.tick();
    assert(rename_issue_q.size() == 1);

    issue.tick();
    assert(rename_issue_q.empty());

    // 周期 1：issue_execute.tick 后读侧可见上周期发射结果

    return EXIT_SUCCESS;
}
