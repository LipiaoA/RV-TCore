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
        agu.bind_queues(&lsu.store_q(), &lsu.load_q(), &lsu);
        agu2.bind_queues(&lsu.store_q(), &lsu.load_q(), &lsu);
        std_fu.bind_lsu(&lsu);
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
        return deps;
    }

    AGUDeps make_agu_deps()
    {
        AGUDeps deps {};
        deps.rob = &rob;
        deps.reg_req_q = &reg_req_q;
        deps.reg_resp_q = &lsu_resp_q;
        deps.reg_bypass_bus = &bypass_q;
        return deps;
    }

    STDDeps make_std_deps()
    {
        STDDeps deps {};
        deps.rob = &rob;
        deps.reg_req_q = &reg_req_q;
        deps.reg_resp_q = &lsu_resp_q;
        deps.reg_bypass_bus = &bypass_q;
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
        if (h.std_fu.commit_ready() && h.std_fu.commit_inst() == inst) {
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

void ack_std(Harness& h, MInstPtr inst)
{
    assert(h.std_fu.commit_ready());
    assert(h.std_fu.commit_inst() == inst);
    h.std_fu.commit_ack();
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

    for (int i = 0; i < 8; ++i) {
        run_cycle(h);
        assert(!h.lsu.commit_ready());
    }

    RegResp resp {};
    resp.valid = true;
    resp.ptag = 9;
    resp.val = 0x10;
    h.bypass_q.push_back(resp);

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
    assert(h.lsu.allocate(std));
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
    assert(h.lsu.allocate(std));
    assert(h.dispatch(sta));
    assert(h.dispatch(std));
    run_cycle(h);
    assert(h.lsu.allocate(load));
    assert(h.dispatch(load));
    assert(run_until_lsu_complete(h, load));
    ack_if_front(h, load);

    RegResp resp {};
    resp.valid = true;
    resp.ptag = 10;
    resp.val = 0x1100;
    h.bypass_q.push_back(resp);

    bool saw_memory_order = false;
    for (int i = 0; i < 16; ++i) {
        run_cycle(h);
        FlushEvent event {};
        while (h.flush_req_q.read(event)) {
            if (event.valid && event.type == FlushType::MEMORY_ORDER
                && event.scope == FlushScope::FROM_RID
                && event.rid.idx == load->rid.idx) {
                saw_memory_order = true;
            }
        }
        if (saw_memory_order) {
            break;
        }
    }
    assert(saw_memory_order);
}

void test_load_lcam_forward_waits_until_cycle_after_execute()
{
    Harness h;

    constexpr u64 token = 99;
    auto* sta = make_sta(0x2600, ROBID{1, 0}, 0x1180, token);
    auto* std = make_std(0x2600, ROBID{2, 0}, 0x123456789abcdef0ULL, token);
    assert(h.lsu.allocate(sta));
    assert(h.lsu.allocate(std));

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

} // namespace

int main()
{
    test_agu_waits_for_source_operand();
    test_store_commit_updates_subsequent_loads();
    test_younger_load_replays_when_older_store_resolves_same_address();
    test_load_lcam_forward_waits_until_cycle_after_execute();
    test_lsu_flush_from_rid_clears_matching_load();
    return EXIT_SUCCESS;
}
