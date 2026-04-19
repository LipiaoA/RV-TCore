
#include "icache.h"
#include "log.h"
using namespace std;
int main()
{
    auto& log = LogSystem::instance();
    log.set_level(LEVEL1);
    log.add_writer(make_shared<ConsoleSink>());
    log.set_mode(LogMode::SYNC);

    // ios::sync_with_stdio(false); cin.tie(nullptr);
    CycleScheduler sched;
    // TLB: 3-level PTW with different latencies
    vector<PTWLevelConfig> levels = {{3}, {5}, {7}};
    MultiLevelTLB tlb(&sched, 12, levels);

    CacheConfig l2cfg(512 * 1024, 64, 8);
    l2cfg.hit_latency = 10;
    l2cfg.miss_penalty = 30;
    l2cfg.install_latency = 5;
    l2cfg.writeback_latency = 12;
    GCache L2(&sched, l2cfg, InclusionPolicy::Inclusive);
    L2.set_name("L2 Cache");

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    icfg.hit_latency = 1;
    icfg.miss_penalty = 3;
    icfg.install_latency = 1;
    icfg.writeback_latency = 8;
    ICache ic(&sched, icfg, &tlb, InclusionPolicy::NonInclusive);
    ic.attach_lower(&L2);
    ic.set_name("ICache");

    CacheConfig dcfg(64 * 1024, 64, 4);
    dcfg.hit_latency = 2;
    dcfg.miss_penalty = 8;
    dcfg.install_latency = 2;
    GCache dc(&sched, dcfg, InclusionPolicy::NonInclusive);
    dc.attach_lower(&L2);
    dc.set_name("DCache");
    // fetch a bundle of instructions
    u64 vpc = 0x1000;
    auto futs = ic.fetch_instructions_async(vpc, 4, icfg.fetch_width);

    // run until ready or timeout
    for (u64 cyc = 0; cyc < 500; ++cyc) {
        LOG_LEVEL1("main", "cycle: %d\n", cyc);
        sched.tick();
        bool done = true;
        for (auto &f : futs)
            if (!f.ready()) {
                done = false;
                break;
            }
        if (done) break;
    }

    // collect results
    for (size_t i = 0; i < futs.size(); ++i) {
        vector<u8> inst;
        bool got = false;
        futs[i].then([&](const vector<u8> &d)
                     { inst=d; got=true; });
        if (!got)
            cout << "instr " << i << " not ready";
        else {
            cout << hex << "PC=0x" << (vpc + i * 4) << " ";
            for (auto b : inst)
                cout << setw(2) << setfill('0') << (int)b;
            cout << endl;
        }
    }

    // print stats
    auto s = ic.get_stats();
    cout << "ICache: accesses=" << s.accesses << " hits=" << s.hits << " misses=" << s.misses
              << " avg_latency=" << (s.accesses ? (double)s.total_latency / s.accesses : 0.0) << "";
    auto s2 = L2.get_stats();
    cout << "L2: accesses=" << s2.accesses << " hits=" << s2.hits << " misses=" << s2.misses
              << " avg_latency=" << (s2.accesses ? (double)s2.total_latency / s2.accesses : 0.0) << endl;

    return 0;
}
