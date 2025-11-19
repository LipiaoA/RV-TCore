
#include <cstdint>
#include <vector>
#include <deque>
#include <optional>
#include <memory>
#include <iostream>
#include <cassert>

#include "fetch_unit.h"

FetchUnit::FetchUnit(const std::string& n)
    : IFetch(n)
{
    // 初始化寄存器都默认为 invalid
    m_pred.clear();
    m_f1_req.clear();
    m_f2_line.clear();
    m_f3_pkt.clear();
    m_f4_pkt.clear();

    m_pc = 0x1000;   // default reset PC
}

void FetchUnit::build() {
    m_sched = new CycleScheduler();

    CacheConfig l2cfg(512 * 1024, 64, 8);
    l2cfg.hit_latency = 10;
    l2cfg.miss_penalty = 30;
    l2cfg.install_latency = 5;
    l2cfg.writeback_latency = 12;

    m_l2_cache = new DCache(m_sched, l2cfg, InclusionPolicy::Inclusive);
    m_l2_cache->set_name("l2 Cache");


    std::vector<PTWLevelConfig> levels = {{3}, {5}, {7}};
    m_tlb = new MultiLevelTLB(m_sched, 12, levels);

    CacheConfig icfg(32 * 1024, 64, 4, 4);
    icfg.hit_latency = 1;
    icfg.miss_penalty = 3;
    icfg.install_latency = 1;
    icfg.writeback_latency = 8;

    m_icache = new ICache(m_sched, icfg, m_tlb);
    m_icache->attach_lower(m_l2_cache);
    m_icache->set_name("ICache");

    CacheConfig dcfg(64 * 1024, 64, 4);
    dcfg.hit_latency = 2;
    dcfg.miss_penalty = 8;
    dcfg.install_latency = 2;

    m_dcache = new DCache(m_sched, dcfg, InclusionPolicy::NonInclusive);
    m_dcache->attach_lower(m_l2_cache);
    m_dcache->set_name("DCache");

}

void FetchUnit::tick()
{
    // 1) 先推进外设（ICache 内部计时）
    m_sched->tick();

    fetch4();
    fetch3();
    fetch2();
    fetch1();
    fetch0();
}

void FetchUnit::redirct(pc_t new_pc, u64 fetch_id)
{
    // 更新 squash 界限与 PC，丢弃 pending icache request 与队列中符合的 entries
    m_squash_id = fetch_id;
    m_pc = new_pc;


    // flush pipeline registers
    // 重定向的时候需要将取指的流水线刷空

    // drop dispatch queue entries >= squash id
    while (!m_dispatch_q.empty() && m_dispatch_q.front().fetch_id >= m_squash_id)
        m_dispatch_q.pop_front();
}

std::optional<fetch_packet_t> FetchUnit::try_dispatch()
{
    if (m_dispatch_q.empty())
        return std::nullopt;
    auto pkt = m_dispatch_q.front();
    m_dispatch_q.pop_front();
    return pkt;
}

/*
 * F0: PC 选择与分支预测
 * - 读取当前 m_pc，调用 predictor
 * - 生成 branch_pred_t 写入 m_f0_out_nxt
 * - 不直接改变 m_pc（真正 advance 在 F3 after assembling）
 */
void FetchUnit::fetch0()
{
    // 如果有 squash 阈值阻止旧包进入，仍允许预测新的PC
    branch_pred_t pred = m_bpu->predict(m_pc);

    m_pred.valid = true;
    m_pred.data = pred;
}

/*
 * F1: I$ 请求阶段
 * - 从 F0 的预测结果决定要请求哪条 cache line（按 line 对齐）
 * - 发起 icache.request_line(line_addr)（非阻塞）
 * - 标记 m_icache_req_pending / m_pending_line_addr 跟踪响应
 */
void FetchUnit::fetch1()
{
    if (!m_pred.valid) {
        m_pred.clear();
        return;
    }

    // choose PC to request: if predicted taken -> pred.target else current PC
    pc_t req_pc = m_pred.data.taken ? m_pred.data.target : m_pc;
    pc_t line_addr = align_line(req_pc);

    // if there's already a pending request to same line, reuse it
    if (m_icache_req_pending && m_pending_line_addr == line_addr) {
        // keep waiting; no new request
        m_f1_req.valid = false;
        return;
    }

    // attempt request
    bool accepted = m_icache->request(line_addr);
    if (accepted) {
        m_icache_req_pending = true;
        m_pending_line_addr = line_addr;
        m_f1_req.valid = true;
        m_f1_req.data = line_addr;
    } else {
        // not accepted (rare in this stub) -> do nothing this cycle
        m_f1_req.clear();
    }
}

/*
 * F2: I$ 返回与对齐
 * - 检查 icache 是否有 ready line (m_pending_line_addr)
 * - 若 ready -> read_line, 放入 m_f2_line_nxt（line bytes）
 * - clear pending flag
 */
void FetchUnit::fetch2()
{
    if (!m_icache_req_pending) {
        m_f2_line.clear();
        return;
    }

    if (m_icache->line_ready(m_pending_line_addr)) {
        bytes_t line = m_icache->read_line(m_pending_line_addr);
        m_icache_req_pending = false;
        m_pending_line_addr = 0;

        m_f2_line.valid = true;
        m_f2_line.data = std::move(line);
    } else {
        // still waiting
        m_f2_line.clear();
    }
}

/*
 * F3: 指令解码准备（拼接、预解码）
 * - 从 F2 的 line bytes 组装最多 k_fetch_width 条 inst
 * - 注入预测信息（来自 F0）
 * - 产生 fetch_packet 写入 m_f3_pkt_nxt
 *
 * NOTE: 这里简化为定长 4-byte 指令；真实实现需要预解码并支持跨行拼接
 */
void FetchUnit::fetch3()
{
    if (!m_f2_line.valid) {
        m_f3_pkt.clear();
        return;
    }

    // start PC for this line: compute from the low byte used when constructing ICache stub
    // (in stub, bytes were filled based on line_addr LSB, but here we track via m_pc)
    // For a robust model we should carry the exact requested PC through stages;
    // For simplicity, assume the packet base pc is m_pc (or predicted target if pred)
    pc_t base_pc = m_pc;
    // TODO: in real design, the exact requested PC (pred target or m_pc) should be carried.

    fetch_packet_t pkt;
    pkt.valid = true;
    pkt.fetch_id = m_next_fetch_id++;
    pkt.base_pc = base_pc;
    pkt.insts.clear();

    // assemble up to k_fetch_width instructions from the line data
    const bytes_t &line = m_f2_line.data;
    size_t offset = base_pc % ICache::k_line_bytes;   // approximate offset inside line
    // Safety clamp
    if (offset >= line.size())
        offset = 0;

    for (unsigned i = 0; i < m_fetch_width; ++i) {
        if (offset + 4 > line.size())
            break;   // would cross line -> stop here (simple behaviour)
        insn_t insn = 0;
        // little endian assemble
        for (int b = 0; b < 4; ++b)
            insn |= static_cast<insn_t>(line[offset + b]) << (8 * b);
        fetch_inst_t finst;
        finst.valid = true;
        finst.pc = base_pc + i * 4;
        finst.insn = insn;
        finst.fetch_id = pkt.fetch_id;
        pkt.insts.push_back(finst);
        offset += 4;
    }

    // Merge prediction metadata from F0 (if available)
    if (m_pred.valid) {
        pkt.pred_taken = m_pred.data.taken;
        pkt.pred_target = m_pred.data.target;
    } else {
        pkt.pred_taken = false;
        pkt.pred_target = 0;
    }

    m_f3_pkt.valid = true;
    m_f3_pkt.data = std::move(pkt);

    // advance m_pc to the next fetch point in this simplified model
    // note: real logic must consider instruction sizes and predicted taken
    if (!m_f3_pkt.data.insts.empty()) {
        m_pc = m_f3_pkt.data.insts.back().pc + 4;
    }
}

/*
 * F4: 输出到 dispatch 队列（FetchBuffer）
 * - 将 F3 生成的 packet 放入 dispatch 队列（若队列满则 stall 不取）
 * - 写入 m_f4_out（保留一份供 debug /观察）
 */
void FetchUnit::fetch4()
{
    if (!m_f3_pkt.valid) {
        m_f4_pkt.clear();
        return;
    }

    // if squash threshold says this should be dropped
    if (m_f3_pkt.data.fetch_id >= m_squash_id && m_squash_id > 0) {
        // drop silently
        m_f4_pkt.clear();
        return;
    }

    // attempt to enqueue into dispatch queue
    if (m_dispatch_q.size() >= m_dispatch_q_capacity) {
        // queue full -> stall front-end (do not consume F3 this cycle)
        // To model stall accurately, we would prevent F3 from committing;
        // in this simplified model, if queue full, we simply don't push and mark F4 as invalid.
        m_f4_pkt.clear();
        return;
    }

    // push packet
    m_dispatch_q.push_back(m_f3_pkt.data);
    m_f4_pkt.valid = true;
    m_f4_pkt.data = m_f3_pkt.data;
}

/* -------------------- Debug / state dump -------------------- */

void FetchUnit::dump_state() const
{
    std::cout << "FetchUnit state:\n";
    std::cout << "  m_pc = 0x" << std::hex << m_pc << std::dec << "\n";
    std::cout << "  next_fetch_id = " << m_next_fetch_id << "\n";
    std::cout << "  icache_pending = " << (m_icache_req_pending ? "yes" : "no") << "\n";
    std::cout << "  dispatch_q.size = " << m_dispatch_q.size() << "\n";
    if (!m_dispatch_q.empty()) {
        std::cout << "  front fetch_id = " << m_dispatch_q.front().fetch_id
                  << " base_pc = 0x" << std::hex << m_dispatch_q.front().base_pc << std::dec << "\n";
    }
    if (m_f4_pkt.valid) {
        std::cout << "  last output fetch_id = " << m_f4_pkt.data.fetch_id << "\n";
    }
}

FetchUnit::~FetchUnit() {
    delete_ptr(m_l2_cache);
    delete_ptr(m_dcache);
    delete_ptr(m_tlb);
    delete_ptr(m_icache);
    delete_ptr(m_sched);
}
