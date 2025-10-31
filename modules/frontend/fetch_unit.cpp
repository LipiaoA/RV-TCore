#include "fetch_unit.h"


// fetch_unit.cpp
// BOOM-like FetchUnit (F0 - F4) reference implementation (single-file).
// Author: example
// Compile: g++ -std=c++17 fetch_unit.cpp -O2 -o fetch_unit

#include <cstdint>
#include <vector>
#include <deque>
#include <optional>
#include <memory>
#include <iostream>
#include <cassert>


static constexpr unsigned k_fetch_width = 4;     // 每包最大指令数
static constexpr unsigned k_icache_line_bytes = 64;
static constexpr unsigned k_icache_latency = 2;  // 模拟 I$ 2 cycle latency


FetchUnit::FetchUnit()
    : m_icache(std::make_unique<ICache>())
    , m_bpu(std::make_unique<BranchPredictor>())
{
    // 初始化寄存器都默认为 invalid
    m_pred.clear();
    m_f1_req.clear();
    m_f2_line.clear();
    m_f3_pkt.clear();
    m_f4_pkt.clear();

    m_pc = 0x1000;   // default reset PC
}

void FetchUnit::tick()
{
    // 1) 先推进外设（ICache 内部计时）
    m_icache->tick();
    run_f4();
    run_f3();
    run_f2();
    run_f1();
    run_f0();
}

/* -------------------- Redirect / Flush -------------------- */

void FetchUnit::redirect(pc_t new_pc, uint64_t fetch_id)
{
    // 更新 squash 界限与 PC，丢弃 pending icache request 与队列中符合的 entries
    m_squash_id = fetch_id;
    m_pc = new_pc;


    // flush pipeline registers
    // 重定向的时候需要将取指的流水线刷空

    // drop dispatch queue entries >= squash id
    while (!m_dispatch_q.empty() && m_dispatch_q.front().fetch_id >= m_squash_from_id)
        m_dispatch_q.pop_front();
}

/* -------------------- try_dispatch -------------------- */

std::optional<fetch_packet_t> FetchUnit::try_dispatch()
{
    if (m_dispatch_q.empty())
        return std::nullopt;
    auto pkt = m_dispatch_q.front();
    m_dispatch_q.pop_front();
    return pkt;
}

/* -------------------- 每阶段实现（职责注释） -------------------- */

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

    m_pred_state.valid = true;
    m_pred_state.data = pred;
}

/*
 * F1: I$ 请求阶段
 * - 从 F0 的预测结果决定要请求哪条 cache line（按 line 对齐）
 * - 发起 icache.request_line(line_addr)（非阻塞）
 * - 标记 m_icache_req_pending / m_pending_line_addr 跟踪响应
 */
void FetchUnit::fetch1()
{
    if (!m_pred_state.valid) {
        m_pred_state.clear();
        return;
    }

    // choose PC to request: if predicted taken -> pred.target else current PC
    pc_t req_pc = m_pred_state.data.taken ? m_pred_state.data.target : m_pc;
    pc_t line_addr = align_line(req_pc);

    // if there's already a pending request to same line, reuse it
    if (m_icache_req_pending && m_pending_line_addr == line_addr) {
        // keep waiting; no new request
        m_f1_req.valid = false;
        return;
    }

    // attempt request
    bool accepted = m_icache->request_line(line_addr);
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
    if (m_f3_pkt.data.fetch_id >= m_squash_from_id && m_squash_from_id > 0) {
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


