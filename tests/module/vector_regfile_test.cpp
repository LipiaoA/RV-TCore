#include <cassert>
#include <cstdlib>

#include "rename/rename_unit.h"
#include "vregfile_unit.h"
#include "tregfile_unit.h"
#include "regfile_unit.h"
#include "rob/rob.h"
#include "module_comm.h"
#include "tools/queue.h"

int main()
{
    ASyncQueue<MInstPtr> decode_rename_q(16);
    ASyncQueue<MInstPtr> rename_issue_q(16);
    ASyncQueue<RegReq> reg_req_q(16);
    ASyncQueue<VecRegReq> vec_reg_req_q(16);
    ASyncQueue<TileRegReq> tile_reg_req_q(16);
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
    RegFileUnit regfile(RegFileConfig {}, regfile_deps);

    VRegFileDeps vregfile_deps {};
    vregfile_deps.vec_reg_req_q = &vec_reg_req_q;
    vregfile_deps.vpu_resp_q = &vpu_resp_q;
    vregfile_deps.wakeup_bus = &wakeup_bus;
    vregfile_deps.flush_event = &flush_event;
    VRegFileConfig vregfile_conf {};
    vregfile_conf.capacity = 64;
    vregfile_conf.vlmax_bytes = VLEN_BYTES;
    VRegFileUnit vregfile(vregfile_conf, vregfile_deps);

    TRegFileDeps tregfile_deps {};
    tregfile_deps.tile_reg_req_q = &tile_reg_req_q;
    tregfile_deps.tpu_resp_q = &tpu_resp_q;
    tregfile_deps.wakeup_bus = &wakeup_bus;
    tregfile_deps.flush_event = &flush_event;
    TRegFileUnit tregfile(TRegFileConfig {}, tregfile_deps);

    std::array<int, RV_GPR_SIZE> crat {};
    std::array<int, RV_VREG_SIZE> vcrat {};
    std::array<int, RV_TILE_SIZE> tcrat {};
    for (u32 i = 0; i < RV_VREG_SIZE; ++i) {
        vcrat[i] = static_cast<int>(i);
    }
    for (u32 i = 0; i < RV_TILE_SIZE; ++i) {
        tcrat[i] = static_cast<int>(i);
    }
    std::vector<RatCheckpoint> checkpoints;

    RenameDeps rename_deps {};
    rename_deps.decode_rename_q = &decode_rename_q;
    rename_deps.rob = &rob;
    rename_deps.rename_issue_q = &rename_issue_q;
    rename_deps.regfile = &regfile;
    rename_deps.vregfile = &vregfile;
    rename_deps.tregfile = &tregfile;
    rename_deps.crat = &crat;
    rename_deps.vcrat = &vcrat;
    rename_deps.tcrat = &tcrat;
    rename_deps.checkpoints = &checkpoints;
    rename_deps.flush_event = &flush_event;
    RenameConfig rename_conf {};
    rename_conf.width = 1;
    rename_conf.prf_capacity = 16;
    rename_conf.vprf_capacity = 64;
    rename_conf.tprf_capacity = 16;
    RenameUnit rename(rename_conf, rename_deps);
    rename.build();

    MInstPtr vec_uop = new MicroOp();
    vec_uop->valid = true;
    vec_uop->type = InstType::VECTOR;
    vec_uop->pc = 0x3000;
    vec_uop->vs1.valid = true;
    vec_uop->vs1.atag = 1;
    vec_uop->vs2.valid = true;
    vec_uop->vs2.atag = 2;
    vec_uop->vd.valid = true;
    vec_uop->vd.atag = 3;

    assert(decode_rename_q.write(vec_uop));
    decode_rename_q.tick();
    rename.tick();
    rename.tick();
    rename.tick();
    rename_issue_q.tick();

    MInstPtr out {};
    assert(rename_issue_q.read(out));
    assert(out->type == InstType::VECTOR);
    assert(out->vs1.ptag == 1);
    assert(out->vs2.ptag == 2);
    assert(out->vd.ptag != 3);
    assert(out->vd.o_ptag == 3);

    VecRegReq write_req {};
    write_req.valid = true;
    write_req.write = true;
    write_req.ptag = out->vd.ptag;
    VecReg payload;
    payload.reset(VLEN_BYTES);
    payload.data[0] = 0x42;
    write_req.val = payload;
    assert(vec_reg_req_q.write(write_req));
    vec_reg_req_q.commit();
    vregfile.tick();
    assert(wakeup_bus.size() == 1);
    assert(wakeup_bus[0].rtyp == RegType::VEC);

    return EXIT_SUCCESS;
}
