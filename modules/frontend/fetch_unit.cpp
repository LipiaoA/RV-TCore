
#include <cstdint>
#include <vector>
#include <deque>
#include <optional>
#include <memory>
#include <iostream>
#include <cassert>

#include "fetch_unit.h"
#include "branch_predictor.h"
#include "decode/decode_unit.h"
#include "icache/dcache.h"
#include "log.h"
#include "util/util.h"

FetchUnit::FetchUnit(const FetchConfig &conf, const IFetchDeps& deps)
    : m_sched(*deps.sched)
    , m_icache(*deps.icache)
    , m_bpu(*deps.bpu)
    , m_fetch_decode_q(*deps.fetch_decode_q)
    , m_fetch_width(conf.fetch_width)
{
    m_pred.clear();
    m_f1_req.clear();
    m_f2_line.clear();
    m_f3_pkt.clear();
    m_f4_pkt.clear();

    m_pc          = 0;
    m_squash_id   = 0;
}

void FetchUnit::build() {}

void FetchUnit::tick()
{
    m_sched.tick();

    fetch4();
    fetch3();
    fetch2();
    fetch1();
    fetch0();
}

void FetchUnit::commit()
{
    m_fetch_decode_q.commit();
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

std::optional<FetchPacket> FetchUnit::try_dispatch()
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
    branch_pred_t pred = m_bpu.predict(m_pc);

    m_pred.valid = true;
    m_pred.data = pred;
    m_pc = pred.taken ? pred.target : m_pc + m_fetch_width * 4;
    LOG_LEVEL1("F0 Predict", "pc:0x%lx  valid:%d taken:%d target:%lx", m_pc, pred.valid, pred.taken, pred.target);

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
        m_f1_req.clear();
        return;
    }

    // choose PC to request: if predicted taken -> pred.target else current PC
    pc_t req_pc = m_pred.data.taken ? m_pred.data.target : m_pc;
    pc_t line_addr = m_icache.align_line(req_pc);

    // if there's already a pending request to same line, reuse it
    if (m_icache_req_pending && m_pending_line_addr == line_addr) {
        return;
    }

    // attempt request
    bool accepted = m_icache.request(line_addr);
    if (accepted) {
        m_icache_req_pending = true;
        m_pending_line_addr = line_addr;
        m_f1_req.valid = true;
        // 在 F1 寄存器中保存本次取指对应的“起始 PC”，供 F2/F3 使用
        m_f1_req.data = req_pc;
        LOG_LEVEL1("F1 Instr Req", "pc:0x%lx line:0x%lx", req_pc, line_addr);
    } else {
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
    // 没有挂起的 ICache 请求，直接清空本阶段输出
    if (!m_icache_req_pending || !m_f1_req.valid) {
        m_f2_line.clear();
        return;
    }

    if (m_icache.line_ready(m_pending_line_addr)) {
        bytes_t line = m_icache.read_line(m_pending_line_addr);
        m_icache_req_pending = false;
        m_pending_line_addr = 0;

        m_f2_line.valid = true;
        m_f2_line.data = std::move(line);
        LOG_LEVEL1("F2 Data Res", "line_addr:0x%lx", m_pending_line_addr);

    } else {
        m_f2_line.clear();
    }
}

/*
 * F3: 指令解码准备（拼接、预解码）
 * - 从 F2 的 line bytes 组装最多 k_fetch_width 条 inst
 * - 注入预测信息（来自 F0）
 * - 产生 fetch_packet 写入 m_f3_pkt_nxt
 * - 取指 PC 必须 4 字节对齐；仅支持 4 字节指令（不支持 2 字节压缩指令）。
 */
void FetchUnit::fetch3()
{
    // - 每条指令固定 4 字节，不支持 2 字节压缩指令。
    // - 若当前剩余字节不足以组成一条完整指令，则停在这里，等待下一条 cache line 合并后再解析。

    static bytes_t s_f3_buffer;      // 累积尚未完全解析的字节
    static pc_t    s_f3_base_pc = 0; // 对应 s_f3_buffer[0] 的 PC
    static bool    s_f3_buf_valid = false;

    // 如果本周期有新的 I$ 行返回，则把它 append 到缓冲中
    if (m_f2_line.valid) {
        const bytes_t &line = m_f2_line.data;

        if (!s_f3_buf_valid) {
            // 新一轮取指窗口：确定本次缓冲起始 PC
            pc_t base_pc = m_f1_req.valid
                ? m_f1_req.data
                : (m_pred.valid && m_pred.data.taken ? m_pred.data.target : m_pc);

            // 取指 PC 必须 4 字节对齐
            if ((base_pc & 3u) != 0) {
                m_f3_pkt.clear();
                return;
            }

            s_f3_base_pc = base_pc;
            s_f3_buffer.clear();

            // 跳过 line 中 base_pc 之前的字节，使 s_f3_buffer[0] 与 base_pc 对齐。
            size_t offset = base_pc - m_icache.align_line(base_pc);
            if (offset > line.size())
                offset = line.size();
            s_f3_buffer.insert(s_f3_buffer.end(), line.begin() + offset, line.end());
            s_f3_buf_valid = true;
        } else {
            // 已有部分字节（可能来自上一条 cache line 的尾部），直接把新 line 整段追加上去
            s_f3_buffer.insert(s_f3_buffer.end(), line.begin(), line.end());
        }
    }

    // 如果缓冲里依然没有字节可用，则本周期无法输出 fetch 包
    if (!s_f3_buf_valid || s_f3_buffer.empty()) {
        m_f3_pkt.clear();
        return;
    }

    // 取指 PC 4 字节对齐检查（解析前再次确认）
    if ((s_f3_base_pc & 3u) != 0) {
        m_f3_pkt.clear();
        return;
    }

    LOG_LEVEL1("F3 Combine", "buffer_bytes:%zu base_pc:0x%lx",
               s_f3_buffer.size(), s_f3_base_pc);

    FetchPacket pkt;
    pkt.valid = true;
    pkt.fetch_id = m_next_fetch_id++;
    pkt.base_pc = s_f3_base_pc;
    pkt.insts.clear();

    // 从缓冲头部开始，逐条解析 4 字节定长指令（不支持 2 字节压缩指令）
    constexpr u8 k_inst_bytes = 4;
    size_t buf_idx = 0;
    pc_t cur_pc  = s_f3_base_pc;

    for (u8 i = 0; i < m_fetch_width; ++i) {
        // 至少需要 4 字节才能组成一条指令
        if (s_f3_buffer.size() - buf_idx < k_inst_bytes) {
            break;
        }

        insn_t insn = 0;
        for (u8 b = 0; b < k_inst_bytes; ++b) {
            insn |= static_cast<insn_t>(s_f3_buffer[buf_idx + b]) << (8 * b);
        }

        FetchInst finst;
        finst.valid    = true;
        finst.pc       = cur_pc;
        finst.insn     = insn;
        finst.fetch_id = pkt.fetch_id;
        finst.size     = k_inst_bytes;

        pkt.insts.push_back(finst);

        PredecodeInfo& pd = pkt.predecode[i];
        DecodeUnit::predecode_one(static_cast<u32>(insn), &pd);
        pd.slot = static_cast<u8>(i);
        pkt.valid_mask |= (1u << i);
        if (pd.is_branch)
            pkt.br_mask |= (1u << i);

        buf_idx += k_inst_bytes;
        cur_pc  += k_inst_bytes;
    }

    // 若本轮一个指令都没解析出来，则无法前进，等待更多字节
    if (pkt.insts.empty()) {
        m_f3_pkt.clear();
        return;
    }

    // 消费掉已经被解析的前缀字节，保留下一条指令的起始处，便于跨 line 拼接
    s_f3_buffer.erase(s_f3_buffer.begin(), s_f3_buffer.begin() + buf_idx);
    s_f3_base_pc = cur_pc;
    if (s_f3_buffer.empty()) {
        s_f3_buf_valid = false;
    }

    // 合并来自 F0 的预测元数据
    if (m_pred.valid) {
        pkt.pred_taken  = m_pred.data.taken;
        pkt.pred_target = m_pred.data.target;
    } else {
        pkt.pred_taken  = false;
        pkt.pred_target = 0;
    }

    m_f3_pkt.valid = true;
    m_f3_pkt.data  = std::move(pkt);

    // 根据最后一条指令的真实长度推进 m_pc
    const auto &last = m_f3_pkt.data.insts.back();
    m_pc = last.pc + last.size;
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
    LOG_LEVEL1("F4 Dispatch", "test");

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
    m_fetch_decode_q.write(m_f4_pkt.data);
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

FetchUnit::~FetchUnit() { }
