#include <cassert>
#include <cstdlib>
#include <vector>

#include "execute/agu.h"
#include "execute/stx.h"
#include "execute/lsu/lsu.h"
#include "icache/dcache.h"
#include "isa/opcode.h"
#include "memory/mem.h"
#include "memory/tlb.h"
#include "perf/perf_def.h"
#include "rob/rob.h"
#include "tools/queue.h"

namespace {

struct Harness {
    ASyncQueue<RegReq> reg_req_q { 32 };
    ASyncQueue<FlushEvent> flush_req_q { 16 };
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> lsu_resp_q;
    FixedArray<RegResp, MAX_REGISTER_REQ_CAPACITY> bypass_q;
    std::vector<LQEntry> load_q;
    std::vector<SQEntry> store_q;
    std::vector<WakeupEvent> wakeup_bus;
    FlushEvent flush_event {};
    PerfStats stats {};
    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels { {1} };
    MultiLevelTLB tlb { &sched, 12, levels };
    Memory mem;
    DCache l2 { &sched, CacheConfig(static_cast<size_t>(64 * 1024), 64, 4, 1) };
    DCache dcache { &sched, CacheConfig(static_cast<size_t>(16 * 1024), 64, 2, 1) };
    ROBDeps rob_deps {};
    ROB rob;
    LSU lsu;
    AGU agu;
    AGU agu2;
    STD std_fu;

    Harness()
        : rob(make_rob_config(), init_rob_deps())
        , lsu(make_lsu_config(), make_lsu_deps())
        , agu(make_agu_config(), make_agu_deps())
        , agu2(make_agu_config(), make_agu_deps())
        , std_fu(make_std_config(), make_std_deps())
    {
        mem.add_segment(0x1000, 0x2000);
        l2.set_mem(&mem);
        dcache.attach_lower(&l2);
        lsu.build();
    }

    ROBConfig make_rob_config()
    {
        ROBConfig conf {};
        conf.capacity = 32;
        return conf;
    }

    ROBDeps& init_rob_deps()
    {
        rob_deps.flush_event = &flush_event;
        return rob_deps;
    }

    LSUConfig make_lsu_config()
    {
        LSUConfig conf {};
        conf.load_q_size = 16;
        conf.store_q_size = 16;
        conf.issue_width = 2;
        conf.dtlb_hit_latency = 1;
        conf.dcache_line_size = 64;
        return conf;
    }

    AGUConfig make_agu_config()
    {
        AGUConfig conf {};
        conf.stages = 2;
        return conf;
    }

    STDConfig make_std_config()
    {
        STDConfig conf {};
        conf.stages = 2;
        return conf;
    }

    LSUDeps make_lsu_deps()
    {
        LSUDeps deps {};
        deps.rob = &rob;
        deps.dcache = &dcache;
        deps.dtlb = &tlb;
        deps.mem = &mem;
        deps.sched = &sched;
        deps.reg_req_q = &reg_req_q;
        deps.reg_resp_q = &lsu_resp_q;
        deps.reg_bypass_bus = &bypass_q;
        deps.flush_req_q = &flush_req_q;
        deps.wakeup_bus = &wakeup_bus;
        deps.lsu_ld_q = &load_q;
        deps.lsu_st_q = &store_q;
        deps.flush_event = &flush_event;
        deps.perf_stats = &stats;
        return deps;
    }

    AGUDeps make_agu_deps()
    {
        AGUDeps deps {};
        deps.lsu_st_q = &store_q;
        deps.lsu_ld_q = &load_q;
        deps.rob = &rob;
        deps.reg_req_q = &reg_req_q;
        deps.reg_resp_q = &lsu_resp_q;
        deps.reg_bypass_bus = &bypass_q;
        deps.lsu = &lsu;
        deps.perf_stats = &stats;
        return deps;
    }

    STDDeps make_std_deps()
    {
        STDDeps deps {};
        deps.lsu_st_q = &store_q;
        deps.lsu_ld_q = &load_q;
        deps.rob = &rob;
        deps.reg_req_q = &reg_req_q;
        deps.reg_resp_q = &lsu_resp_q;
        deps.reg_bypass_bus = &bypass_q;
        deps.lsu = &lsu;
        deps.perf_stats = &stats;
        return deps;
    }

    bool dispatch(MInstPtr inst)
    {
        if (!lsu.can_dispatch(inst)) {
            return false;
        }
        if (inst->type == InstType::STD) {
            return std_fu.dispatch(inst);
        }
        if (agu.dispatch(inst)) {
            return true;
        }
        return agu2.dispatch(inst);
    }
};

MInstPtr make_load(u64 pc, ROBID rid, u64 addr, i32 ptag = 1)
{
    auto* inst = new MicroOp();
    inst->valid = true;
    inst->state = InstState::ISSUED;
    inst->opcode = OP_LD;
    inst->type = InstType::LOAD;
    inst->pc = pc;
    inst->rid = rid;
    inst->rs1.valid = false;
    inst->imm = static_cast<i64>(addr);
    inst->rd.valid = true;
    inst->rd.ptag = ptag;
    inst->ls_width = 8;
    return inst;
}

MInstPtr make_sta(u64 pc, ROBID rid, u64 addr, u64 token)
{
    auto* inst = new MicroOp();
    inst->valid = true;
    inst->state = InstState::ISSUED;
    inst->opcode = OP_SD;
    inst->type = InstType::STA;
    inst->pc = pc;
    inst->rid = rid;
    inst->rs1.valid = false;
    inst->imm = static_cast<i64>(addr);
    inst->store_id = token;
    inst->ls_width = 8;
    return inst;
}

MInstPtr make_std(u64 pc, ROBID rid, u64 data, u64 token)
{
    auto* inst = new MicroOp();
    inst->valid = true;
    inst->state = InstState::ISSUED;
    inst->opcode = OP_SD;
    inst->type = InstType::STD;
    inst->pc = pc;
    inst->rid = rid;
    inst->rs2.valid = false;
    inst->data = data;
    inst->store_id = token;
    inst->ls_width = 8;
    return inst;
}

void run_cycle(Harness& h)
{
    h.agu.commit();
    h.agu2.commit();
    h.std_fu.commit();
    h.lsu.commit();
    h.agu.tick();
    h.agu2.tick();
    h.std_fu.tick();
    h.lsu.tick();
    h.sched.tick();
    h.flush_req_q.commit();
}

bool run_until_lsu_complete(Harness& h, MInstPtr inst, int max_cycles = 256)
{
    for (int i = 0; i < max_cycles; ++i) {
        run_cycle(h);
        if (h.lsu.commit_ready() && h.lsu.commit_inst() == inst) {
            return true;
        }
        if (h.lsu.commit_ready()) {
            h.lsu.commit_ack();
        }
    }
    return false;
}

bool run_until_std_complete(Harness& h, MInstPtr inst, int max_cycles = 256)
{
    for (int i = 0; i < max_cycles; ++i) {
        run_cycle(h);
        if (inst && inst->std_done) {
            return true;
        }
    }
    return false;
}

void ack_if_front(Harness& h, MInstPtr inst)
{
    assert(h.lsu.commit_ready());
    assert(h.lsu.commit_inst() == inst);
    h.lsu.commit_ack();
}

void ack_std(Harness&, MInstPtr inst)
{
    assert(inst && inst->std_done);
    assert(inst->data_ready);
}

void test_agu_waits_for_source_operand()
{
    Harness h;
    h.mem.store64(0x1010, 0x55aa);

    auto* load = make_load(0x2000, ROBID{1, 0}, 0, 3);
    load->rs1.valid = true;
    load->rs1.ptag = 9;
    load->imm = 0x1000;
    assert(h.lsu.allocate(load));
    assert(h.dispatch(load));

    // AGU drops uops whose rs1 is not yet available; Issue would redispatch.
    for (int i = 0; i < 8; ++i) {
        run_cycle(h);
        assert(!h.lsu.commit_ready());
        assert(h.dispatch(load));
    }

    RegResp resp {};
    resp.valid = true;
    resp.ptag = 9;
    resp.val = 0x10;
    h.lsu_resp_q.push_back(resp);

    assert(h.dispatch(load));
    assert(run_until_lsu_complete(h, load));
    assert(load->rd.val == 0x55aa);
}

void test_store_commit_updates_subsequent_loads()
{
    Harness h;
    h.mem.store64(0x1080, 0x1111);

    auto* first_load = make_load(0x2100, ROBID{1, 0}, 0x1080, 4);
    assert(h.lsu.allocate(first_load));
    assert(h.dispatch(first_load));
    assert(run_until_lsu_complete(h, first_load));
    assert(first_load->rd.val == 0x1111);
    ack_if_front(h, first_load);
    h.lsu.retire_load(first_load->rid);

    constexpr u64 token = 77;
    auto* sta = make_sta(0x2200, ROBID{2, 0}, 0x1080, token);
    auto* std = make_std(0x2200, ROBID{3, 0}, 0x2222, token);
    assert(h.lsu.allocate(sta));
    // STD shares the STA-allocated SQ entry via store_id/token.
    std->store_id = sta->store_id;
    assert(h.dispatch(sta));
    assert(h.dispatch(std));
    assert(run_until_lsu_complete(h, sta));
    ack_if_front(h, sta);
    assert(run_until_std_complete(h, std));
    ack_std(h, std);
    assert(h.lsu.store_commit_ready(sta->rid));
    h.lsu.commit_store(sta->rid);

    auto* second_load = make_load(0x2300, ROBID{4, 0}, 0x1080, 5);
    assert(h.lsu.allocate(second_load));
    assert(h.dispatch(second_load));
    assert(run_until_lsu_complete(h, second_load));
    assert(second_load->rd.val == 0x2222);
}

void test_younger_load_replays_when_older_store_resolves_same_address()
{
    Harness h;
    h.mem.store64(0x1100, 0xaaaa);

    constexpr u64 token = 88;
    auto* sta = make_sta(0x2400, ROBID{1, 0}, 0, token);
    sta->rs1.valid = true;
    sta->rs1.ptag = 10;
    auto* std = make_std(0x2400, ROBID{2, 0}, 0xbbbb, token);
    auto* load = make_load(0x2500, ROBID{3, 0}, 0x1100, 6);

    assert(h.lsu.allocate(sta));
    std->store_id = sta->store_id;
    assert(h.dispatch(sta));
    assert(h.dispatch(std));
    // STA waits for rs1; redispatch while providing address via reg response.
    for (int i = 0; i < 4; ++i) {
        run_cycle(h);
        (void)h.dispatch(sta);
        (void)h.dispatch(std);
    }
    assert(h.lsu.allocate(load));
    assert(h.dispatch(load));
    assert(run_until_lsu_complete(h, load));
    ack_if_front(h, load);

    RegResp resp {};
    resp.valid = true;
    resp.ptag = 10;
    resp.val = 0x1100;
    h.lsu_resp_q.clear();
    h.lsu_resp_q.push_back(resp);
    assert(h.dispatch(sta));

    // Current LSU uses lightweight misspec recovery (LoadDepKill), not MEMORY_ORDER flush.
    bool saw_load_dep_kill = false;
    for (int i = 0; i < 32; ++i) {
        run_cycle(h);
        (void)h.dispatch(sta);
        (void)h.dispatch(std);
        for (const auto& event : h.wakeup_bus) {
            if (event.valid && event.load_dep_kill && event.ptag == load->rd.ptag
                && event.rid.idx == load->rid.idx) {
                saw_load_dep_kill = true;
                break;
            }
        }
        if (saw_load_dep_kill) {
            break;
        }
    }
    assert(saw_load_dep_kill);
    LQEntry* ld = h.lsu.find_load_by_id(load->load_idx);
    assert(ld != nullptr);
    assert(ld->violation_reported);
}

void test_load_lcam_forward_waits_until_cycle_after_execute()
{
    Harness h;

    constexpr u64 token = 99;
    auto* sta = make_sta(0x2600, ROBID{1, 0}, 0x1180, token);
    auto* std = make_std(0x2600, ROBID{2, 0}, 0x123456789abcdef0ULL, token);
    assert(h.lsu.allocate(sta));
    std->store_id = sta->store_id;

    SQEntry* st = h.lsu.find_store_by_id(sta->store_id);
    assert(st != nullptr);
    st->addr = 0x1180;
    st->pa = 0x1180;
    st->addr_ready = true;
    st->addr_is_virtual = false;
    st->data = 0x123456789abcdef0ULL;
    st->data_ready = true;
    st->ls_width = 8;

    auto* load = make_load(0x2700, ROBID{3, 0}, 0x1180, 7);
    assert(h.lsu.allocate(load));
    LQEntry* ld = h.lsu.find_load_by_id(load->load_idx);
    assert(ld != nullptr);
    ld->addr = 0x1180;
    ld->addr_ready = true;

    run_cycle(h); // schedule the load into LSU execute.
    assert(!ld->forward_std_val);
    assert(!ld->executed);

    run_cycle(h); // execute/TLB stage only; LCAM forwarding is next cycle.
    assert(!ld->forward_std_val);
    assert(!ld->executed);

    run_cycle(h); // s1 LCAM may now kill the cache request and forward.
    assert(ld->forward_std_val);
    assert(ld->executed);
}

void test_lsu_flush_from_rid_clears_matching_load()
{
    Harness h;

    auto* load = make_load(0x2800, ROBID{4, 0}, 0x1200, 8);
    assert(h.lsu.allocate(load));
    assert(h.lsu.find_load_by_id(load->load_idx) != nullptr);

    FlushEvent event {};
    event.valid = true;
    event.scope = FlushScope::FROM_RID;
    event.rid = load->rid;
    h.lsu.flush(event);

    assert(h.lsu.find_load_by_id(load->load_idx) == nullptr);
    assert(!load->valid);
    assert(load->state == InstState::FLUSHED);
}

void test_store_set_blocks_load_after_training()
{
    Harness h;
    h.mem.store64(0x1300, 0x1111);

    // 第一次：老 store 地址晚到，年轻 load 先执行 → misspec + StoreSet 训练。
    constexpr u64 token1 = 201;
    auto* sta1 = make_sta(0x3000, ROBID{1, 0}, 0, token1);
    sta1->rs1.valid = true;
    sta1->rs1.ptag = 20;
    auto* std1 = make_std(0x3000, ROBID{2, 0}, 0x2222, token1);
    auto* load1 = make_load(0x3100, ROBID{3, 0}, 0x1300, 21);

    assert(h.lsu.allocate(sta1));
    std1->store_id = sta1->store_id;
    assert(h.dispatch(sta1));
    assert(h.dispatch(std1));
    for (int i = 0; i < 4; ++i) {
        run_cycle(h);
        (void)h.dispatch(sta1);
        (void)h.dispatch(std1);
    }
    assert(h.lsu.allocate(load1));
    assert(h.dispatch(load1));
    assert(run_until_lsu_complete(h, load1));
    ack_if_front(h, load1);

    RegResp resp1 {};
    resp1.valid = true;
    resp1.ptag = 20;
    resp1.val = 0x1300;
    h.lsu_resp_q.clear();
    h.lsu_resp_q.push_back(resp1);
    assert(h.dispatch(sta1));

    bool trained = false;
    for (int i = 0; i < 32; ++i) {
        run_cycle(h);
        (void)h.dispatch(sta1);
        (void)h.dispatch(std1);
        LQEntry* ld1 = h.lsu.find_load_by_id(load1->load_idx);
        if (ld1 && ld1->violation_reported) {
            trained = true;
            break;
        }
    }
    assert(trained);
    assert(load1->ssid != 0);

    // 提交并清掉第一组，保留 SSIT 训练结果。
    assert(run_until_std_complete(h, std1));
    ack_std(h, std1);
    assert(run_until_lsu_complete(h, sta1));
    ack_if_front(h, sta1);
    assert(h.lsu.store_commit_ready(sta1->rid));
    h.lsu.commit_store(sta1->rid);
    h.lsu.retire_load(load1->rid);

    // 第二次：同 PC 的 store 仍未出地址时，load 应被 StoreSet 挡住。
    constexpr u64 token2 = 202;
    auto* sta2 = make_sta(0x3000, ROBID{4, 0}, 0, token2);
    sta2->rs1.valid = true;
    sta2->rs1.ptag = 22;
    auto* std2 = make_std(0x3000, ROBID{5, 0}, 0x3333, token2);
    auto* load2 = make_load(0x3100, ROBID{6, 0}, 0x1300, 23);

    assert(h.lsu.allocate(sta2));
    std2->store_id = sta2->store_id;
    assert(h.dispatch(sta2));
    assert(h.dispatch(std2));
    for (int i = 0; i < 4; ++i) {
        run_cycle(h);
        (void)h.dispatch(sta2);
        (void)h.dispatch(std2);
    }

    assert(h.lsu.allocate(load2));
    LQEntry* ld2 = h.lsu.find_load_by_id(load2->load_idx);
    assert(ld2 != nullptr);
    ld2->addr = 0x1300;
    ld2->addr_ready = true;

    bool blocked = false;
    for (int i = 0; i < 8; ++i) {
        run_cycle(h);
        if (ld2->waiting_on_store && ld2->wait_store_id == sta2->store_id) {
            blocked = true;
            break;
        }
        assert(!ld2->executed);
        assert(!ld2->in_flight);
    }
    assert(blocked);
    assert(!ld2->executed);
    assert(!ld2->in_flight);
}

} // namespace

int main()
{
    test_agu_waits_for_source_operand();
    test_store_commit_updates_subsequent_loads();
    test_younger_load_replays_when_older_store_resolves_same_address();
    test_load_lcam_forward_waits_until_cycle_after_execute();
    test_lsu_flush_from_rid_clears_matching_load();
    test_store_set_blocks_load_after_training();
    return EXIT_SUCCESS;
}
