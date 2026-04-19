#include <cassert>
#include <cstdlib>
#include <memory>
#include "log.h"

#include "fetch/fetch_unit.h"
#include "bpred/branch_predictor.h"
#include "icache/icache.h"
#include "memory/cache.h"
#include "memory/tlb.h"
#include "tools/queue.h"
#include "util/util.h"

namespace {

class FakeICache : public ICache {
public:
    FakeICache(CycleScheduler* s, const CacheConfig& cfg, MultiLevelTLB* tlb)
        : ICache(s, cfg, tlb)
    {}

    bool request(pc_t line_addr) override
    {
        (void)line_addr;
        return true;
    }

    bool line_ready(pc_t line_addr) const override
    {
        (void)line_addr;
        return true;
    }

    bytes_t read_line(pc_t line_addr) override
    {
        (void)line_addr;
        bytes_t line(k_line_bytes, 0);
        const u32 insn = 0x00000013u;
        const size_t off = static_cast<size_t>(0x1000 % k_line_bytes);
        for (int i = 0; i < 4; ++i) {
            line[off + static_cast<size_t>(i)] = static_cast<u8>((insn >> (8 * i)) & 0xff);
        }
        return line;
    }
};

} // namespace
using namespace std;
// FetchUnit::tick 内部顺序为 fetch4→…→fetch0；对 fetch_decode 的 write 在流水末端产生。
// 每个仿真周期：fetch.tick() 后接 fetch_decode.tick()，使本周期写入的 FetchPacket 进入读侧。

int main()
{
    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels = {{1}};
    MultiLevelTLB tlb(&sched, 12, levels);

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    FakeICache icache(&sched, icfg, &tlb);

    BranchPredictor bpu{"fetch_test"};
    for (int i = 0; i < 4096; ++i) {
        bpu.update(0x1000, false, 0);
    }

    auto& log = LogSystem::instance();
    log.set_level(LogLevel::LEVEL3);

    log.add_writer(make_shared<ConsoleSink>());

    // 同步模式
    log.set_mode(LogMode::SYNC);
    LOG_LEVEL1("FetchTest", "Starting in synchronous mode");

    ASyncQueue<FetchPacket> fetch_decode_q(32);

    IFetchDeps deps {};
    deps.sched = &sched;
    deps.icache = &icache;
    deps.tlb = &tlb;
    deps.bpu = &bpu;
    deps.fetch_decode_q = &fetch_decode_q;

    FetchConfig fetch_conf {};
    fetch_conf.fetch_width = 4;
    FetchUnit fetch(fetch_conf, deps);
    fetch.build();

    int first_nonempty_cycle = -1;
    for (int c = 0; c < 64; ++c) {
        cout << "Cycle " << c << endl;
        assert(fetch_decode_q.empty());
        fetch.tick();
        assert(fetch_decode_q.empty());
        fetch_decode_q.tick();
        if (fetch_decode_q.size() > 0) {
            first_nonempty_cycle = c;
            break;
        }
        cout << endl;
    }
    assert(first_nonempty_cycle >= 0);

    FetchPacket got {};
    assert(fetch_decode_q.read(got));
    assert(got.valid);
    assert(!got.insts.empty());
    assert(got.insts[0].valid);
    assert(got.insts[0].pc == 0x1000);
    assert(got.insts[0].insn == 0x00000013u);

    return EXIT_SUCCESS;
}
