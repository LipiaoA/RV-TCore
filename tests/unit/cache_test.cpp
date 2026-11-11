#include <cassert>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "icache.h"
#include "log.h"
#include "mem.h"

using namespace std;

int main()
{
    auto& log = LogSystem::instance();
    log.set_level(LEVEL1);
    log.add_writer(make_shared<ConsoleSink>());
    log.set_mode(LogMode::SYNC);

    CycleScheduler sched;
    vector<PTWLevelConfig> levels = {{3}, {5}, {7}};
    MultiLevelTLB tlb(&sched, 12, levels);

    Memory mem;
    mem.add_segment(0x1000, 0x2000);
    for (u64 off = 0; off < 64; off += 4) {
        mem.store32(0x1000 + off, static_cast<u32>(0xA0000000u | static_cast<u32>(off)));
    }

    CacheConfig l2cfg(512 * 1024, 64, 8);
    l2cfg.hit_latency = 10;
    l2cfg.miss_penalty = 30;
    l2cfg.install_latency = 5;
    l2cfg.writeback_latency = 12;
    l2cfg.inclusion_policy = InclusionPolicy::Inclusive;
    GCache L2(&sched, l2cfg);
    L2.set_name("L2 Cache");
    L2.set_mem(&mem);

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    icfg.hit_latency = 1;
    icfg.miss_penalty = 3;
    icfg.install_latency = 1;
    icfg.writeback_latency = 8;
    icfg.inclusion_policy = InclusionPolicy::Inclusive;
    ICache ic(&sched, icfg, &tlb);
    ic.attach_lower(&L2);
    ic.set_name("ICache");

    const u64 vpc = 0x1000;
    IFetchReq req {};
    req.valid = true;
    req.pc = vpc;
    req.line_addr = ic.align_line(vpc);
    req.epoch = 1;
    assert(ic.request(req));

    bool ready = false;
    for (u64 cyc = 0; cyc < 500; ++cyc) {
        LOG_LEVEL1("main", "cycle: %llu\n", static_cast<unsigned long long>(cyc));
        sched.tick();
        if (ic.line_ready(vpc)) {
            ready = true;
            break;
        }
    }
    assert(ready);

    IFetchResp resp = ic.read_line(vpc);
    assert(resp.valid);
    assert(resp.line_addr == ic.align_line(vpc));
    cout << hex << "line=0x" << resp.line_addr << " data0=0x"
         << setw(2) << setfill('0') << static_cast<int>(resp.data[0]) << endl;

    auto s = ic.get_stats();
    cout << "ICache: accesses=" << s.accesses << " hits=" << s.hits << " misses=" << s.misses
         << " avg_latency="
         << (s.accesses ? static_cast<double>(s.total_latency) / s.accesses : 0.0) << endl;
    auto s2 = L2.get_stats();
    cout << "L2: accesses=" << s2.accesses << " hits=" << s2.hits << " misses=" << s2.misses
         << " avg_latency="
         << (s2.accesses ? static_cast<double>(s2.total_latency) / s2.accesses : 0.0) << endl;

    return EXIT_SUCCESS;
}
