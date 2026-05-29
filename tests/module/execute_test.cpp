#include <cassert>
#include <cstdlib>
#include <vector>

#include "execute/alu.h"
#include "execute/bru.h"
#include "regfile_unit.h"
#include "rob/rob.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// Execute-side wakeup sources: ALU/BRU announce bypass readiness in R1, and
// RegFile announces writeback readiness when a physical register is written.

int main()
{
    ASyncQueue<RegReq> reg_req_q(32);
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
    assert(wakeup_bus.size() == 1);
    assert(wakeup_bus[0].valid);
    assert(wakeup_bus[0].type == WAKEUP_BYPASS);
    assert(wakeup_bus[0].ptag == alu_inst->rd.ptag);
    wakeup_bus.clear();

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

    return EXIT_SUCCESS;
}
