#include <cassert>
#include <cstdlib>

#include "commit/commit_unit.h"
#include "rename/rename_unit.h"
#include "regfile_unit.h"
#include "rob/rob.h"
#include "module_comm.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// CommitUnit resolves completed execute results before retiring ROB head entries.
// Retire must update committed RAT state and discard checkpoints that are no
// longer needed by younger branch recovery.

int main()
{
    ASyncQueue<MInstPtr> decode_rename_q(16);
    ASyncQueue<MInstPtr> rename_issue_q(16);
    ASyncQueue<MInstPtr> inst_commit_q(16);
    ASyncQueue<RegReq> reg_req_q(16);
    ASyncQueue<FlushEvent> flush_req_q(16);
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
    regfile_conf.capacity = 16;
    RegFileUnit regfile(regfile_conf, regfile_deps);

    std::array<int, RV_GPR_SIZE> crat {};
    crat.fill(0);
    crat[1] = 1;
    std::vector<RatCheckpoint> checkpoints;

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.rename_issue_q = &rename_issue_q;
    rename_deps.regfile = &regfile;
    rename_deps.flush_event = &flush_event;
    rename_deps.crat = &crat;
    rename_deps.checkpoints = &checkpoints;
    RenameConfig rename_conf {};
    rename_conf.width = 1;
    rename_conf.prf_capacity = 16;
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    CommitDeps commit_deps {};
    commit_deps.inst_commit_q = &inst_commit_q;
    commit_deps.rob = &rob;
    commit_deps.rename = &rename;
    commit_deps.flush_event = &flush_event;
    commit_deps.flush_req_q = &flush_req_q;
    commit_deps.reg_req_q = &reg_req_q;
    commit_deps.crat = &crat;
    commit_deps.checkpoints = &checkpoints;
    CommitConfig commit_conf {};
    commit_conf.commit_width = 1;
    commit_conf.resolve_width = 1;
    CommitUnit commit(commit_conf, commit_deps);
    commit.build();

    MInstPtr uop = new MicroOp();
    uop->valid = true;
    uop->opcode = OP_ADDI;
    uop->type = InstType::INT;
    uop->pc = 0x1000;
    uop->inst_name = "addi";
    uop->state = InstState::ISSUED;
    uop->rd.valid = true;
    uop->rd.atag = 1;
    uop->rd.ptag = 8;
    uop->rd.o_ptag = 1;
    uop->rd.val = 0x1234;

    auto rid = rob.alloc(uop);
    assert(rid);
    uop->rid = rid.value();
    assert(rob.size() == 1);

    RatCheckpoint cp {};
    cp.valid = true;
    cp.rid = uop->rid;
    cp.c_id = 1;
    checkpoints.push_back(cp);

    assert(inst_commit_q.write(uop));
    inst_commit_q.commit();

    // Cycle 0: execute completion is resolved and written back, but the ROB
    // head was not executable at the beginning of the cycle, so it stays.
    commit.tick();
    assert(rob.size() == 1);
    assert(uop->state == InstState::EXECUTED);
    reg_req_q.commit();
    RegReq req {};
    assert(reg_req_q.read(req));
    assert(req.valid);
    assert(req.write);
    assert(req.ptag == 8);
    assert(req.val == 0x1234);
    assert(reg_req_q.empty());

    // Cycle 1: retired architectural state reflects the destination mapping.
    commit.tick();
    assert(rob.empty());
    assert(uop->state == InstState::RETIRED);
    assert(crat[1] == 8);
    assert(checkpoints.empty());

    return EXIT_SUCCESS;
}
