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

    bool request(const IFetchReq& req) override
    {
        m_last_epoch = req.epoch;
        return true;
    }

    bool line_ready(pc_t line_addr) override
    {
        (void)line_addr;
        return true;
    }

    IFetchResp read_line(pc_t line_addr) override
    {
        IFetchResp resp {};
        resp.valid = true;
        resp.line_addr = align_line(line_addr);
        resp.epoch = m_last_epoch;

        const u32 insn = 0x00000013u;
        for (size_t off = 0; off < sizeof(resp.data); off += 4) {
            for (int i = 0; i < 4; ++i) {
                resp.data[off + static_cast<size_t>(i)] = static_cast<u8>((insn >> (8 * i)) & 0xff);
            }
        }
        return resp;
    }

private:
    u64 m_last_epoch { 0 };
};

} // namespace
using namespace std;
// FetchUnit::tick 内部顺序为 fetch5→…→fetch0；对 fetch_decode 的 write 在流水末端产生。
// 每个仿真周期：fetch.tick() 后接 fetch_decode.tick()，使本周期写入的 FetchPacket 进入读侧。

int main()
{
    CycleScheduler sched;
    std::vector<PTWLevelConfig> levels = {{1}};
    MultiLevelTLB tlb(&sched, 12, levels);

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    FakeICache icache(&sched, icfg, &tlb);

    BPConfig bp_conf {};
    bp_conf.bim_config.fetch_width = 4;
    BranchPredictor bpu { bp_conf };

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
    deps.bpu = &bpu;
    deps.fetch_decode_q = &fetch_decode_q;
    FlushEvent flush_event {};
    deps.flush_event = &flush_event;

    FetchConfig fetch_conf {};
    fetch_conf.fetch_width = 4;
    fetch_conf.group_size = 32;
    fetch_conf.bank_size = 16;
    FetchUnit fetch(fetch_conf, deps);
    fetch.build();
    fetch.redirct(0x103d4);

    std::vector<FetchPacket> packets;
    for (int c = 0; c < 64; ++c) {
        cout << "Cycle " << c << endl;
        fetch.tick();
        fetch_decode_q.tick();
        FetchPacket pkt {};
        while (fetch_decode_q.read(pkt)) {
            if (pkt.valid && !pkt.insts.empty()) {
                packets.push_back(pkt);
            }
            pkt = {};
        }
        if (packets.size() >= 2) {
            break;
        }
        cout << endl;
    }
    assert(packets.size() >= 2);

    const auto& first = packets[0];
    const auto& second = packets[1];
    assert(first.base_pc == 0x103d4);
    assert(first.insts.back().pc == 0x103e0);
    assert(second.base_pc == second.insts.front().pc);
    assert(second.base_pc == first.insts.back().pc + first.insts.back().size);
    assert(second.insts.front().pc == first.insts.back().pc + first.insts.back().size);

    return EXIT_SUCCESS;
}
