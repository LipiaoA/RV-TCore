
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
#include "perf/perf_stats.h"
#include "util/util.h"

FetchUnit::FetchUnit(const FetchConfig &conf, const IFetchDeps& deps)
    : m_sched(*deps.sched)
    , m_icache(*deps.icache)
    , m_bpu(*deps.bpu)
    , m_fetch_decode_q(*deps.fetch_decode_q)
    , m_flush_event(*deps.flush_event)
    , m_stats(deps.perf_stats)
    , m_fetch_width(conf.fetch_width)
    , m_group_size(conf.group_size)
    , m_bank_size(conf.bank_size)
{
    m_f3_pkt.clear();
    m_f4_pkt.clear();

    m_pc          = 0;
    m_squash_id   = 0;
    memset(m_run, 0, sizeof(m_run));
}

void FetchUnit::build() {
    m_fetch_id = 1;
    m_epoch = 0;
}

void FetchUnit::tick()
{
    preprocess();
    flush();
    fetch5();
    fetch4();
    fetch3();
    fetch2();
    fetch1();
    fetch0();
    dump_state();
}

void FetchUnit::commit()
{
    m_fetch_decode_q.commit();
}

void FetchUnit::redirct(pc_t new_pc, u64 fetch_id)
{
    m_redirect_pc[RESET_PC].valid = true;
    m_redirect_pc[RESET_PC].pc = new_pc;
    m_pc = new_pc;
    m_state[F0].valid = true;
}

void FetchUnit::preprocess()
{
    memset(m_run, 0, sizeof(m_run));
}

void FetchUnit::fetch0()
{
    if (!m_state[F0].valid) {
        return;
    }
    m_state[F0].vpc = next_pc();
    pc_t line_addr = m_icache.align_line(m_state[F0].vpc);

    if ((m_line_buffer.valid && m_line_buffer.line_addr == line_addr) ||
        (m_ifetch_req.valid && m_pending_line_addr == line_addr)) {
        return;
    } else {
        m_line_buffer.clear();
        m_ifetch_req.valid = true;
        m_ifetch_req.pc = m_state[F0].vpc;
        m_ifetch_req.line_addr = line_addr;
        m_ifetch_req.epoch = m_epoch;
        bool accepted = m_icache.request(m_ifetch_req);
        LOG_LEVEL1("F0 Send Request", "req vpc:0x%lx, line_addr:0x%lx, epoch:%d, accepted:%d",
                    m_state[F0].vpc, line_addr, m_epoch, accepted);
        if (accepted) {
            m_icache_req_pending = true;
            m_pending_line_addr = line_addr;
        }
    }
    m_state[F1].valid = true;
    m_run[F0] = true;
}

void FetchUnit::fetch1()
{
    if (!m_state[F1].valid) {
        m_redirect_pc[RES_F1].valid = false;
        return;
    }

    m_state[F1].vpc = m_state[F0].vpc;
    m_f1_pred = m_bpu.f1_pred(m_state[F1].vpc);
    pc_t fall_addr = (m_state[F1].vpc / m_bank_size) * m_bank_size + m_bank_size;
    bool pred_taken = false;
    for (u32 i = 0; i < m_fetch_width; ++i) {
        if (m_f1_pred.pred[i].valid) {
            pred_taken = true;
            m_redirect_pc[RES_F1].valid = true;
            m_redirect_pc[RES_F1].pc = m_f1_pred.pred[i].target;
            break;
        }
    }
    m_redirect_pc[RES_F1].pc = pred_taken ? m_redirect_pc[RES_F1].pc : fall_addr;

    // TODO: request itlb

    m_state[F2].valid = true;
    m_run[F1] = true;
    return;
}

void FetchUnit::fetch2()
{
    if (!m_state[F2].valid) {
        return;
    }
    m_state[F2].vpc = m_state[F1].vpc;
    m_f2_pred = m_bpu.f2_pred(m_state[F2].vpc);
    if (m_icache.line_ready(m_state[F2].vpc)) {
        IFetchResp resp = m_icache.read_line(m_state[F2].vpc);
        memcpy(m_line_buffer.data, resp.data, sizeof(m_line_buffer.data));
        auto &line = m_line_buffer.data;
        pc_t cur_pc = m_state[F2].vpc;
        u32 idx = bank_idx(cur_pc);
        u32 oft = cur_pc - align_bank(cur_pc) + idx * m_bank_size;

        for (u32 c = 0; c < m_bank_count / m_group_count; ++c) {
            m_line_buffer.bmask |= (1u << idx);
            for (u32 i = oft % m_bank_size; i < m_bank_size; i += 4, oft += 4, cur_pc += 4) {
                F2Inst inst;
                for (int j = 0; j < 4; ++j) {
                    inst.insn |= line[oft + j] << (8 * j);
                }
                inst.pc = cur_pc;
                inst.fetch_id = m_fetch_id++;
                m_inst_buffer.push(inst);
            }
        }
        LOG_LEVEL1("F2 Data Res", "addr:0x%lx", m_state[F2].vpc);
    } else {
        if (m_stats) {
            ++m_stats->frontend.icache_wait_cycles;
        }
        m_redirect_pc[RES_F2].valid = true;
        m_redirect_pc[RES_F2].pc = m_state[F2].vpc;
        m_state[F1].valid = false;
    }
    for (u32 i = 0; i < m_fetch_width; ++i) {
        m_f2_pred.pred[i].target = m_f1_pred.pred[i].target;
        if (m_f2_pred.pred[i].valid) {
            if (m_f2_pred.pred[i].taken) {
                m_redirect_pc[RES_F2].valid = true;
                m_redirect_pc[RES_F2].pc = m_f2_pred.pred[i].target;
            } else {
                m_redirect_pc[RES_F2].valid = true;
                m_redirect_pc[RES_F2].pc = m_state[F2].vpc + 4;
            }
            if (m_f2_pred.pred[i].taken != m_f1_pred.pred[i].taken) {
                m_state[F2].valid = false;
                m_epoch++;
            }
            break;
        }
    }
    m_state[F3].valid = true;
    m_run[F2] = true;
}

void FetchUnit::fetch3()
{
    // - 每条指令固定 4 字节，不支持 2 字节压缩指令。
    // - 若当前剩余字节不足以组成一条完整指令，则停在这里，等待下一条 cache line 合并后再解析。

    // 如果本周期有新的 I$ 行返回，则把它 append 到缓冲中
    if (m_inst_buffer.empty()) {
        return;
    }
    memcpy(&m_f3_bpred, &m_f2_pred, sizeof(m_f3_bpred));
    FetchPacket pkt;
    pkt.valid = true;
    pkt.insts.clear();

    // 从缓冲头部开始，逐条解析 4 字节定长指令（不支持 2 字节压缩指令）
    constexpr u8 k_inst_bytes = 4;

    for (u8 i = 0; i < m_fetch_width && !m_inst_buffer.empty(); ++i) {
        auto& inst = m_inst_buffer.front();
        m_inst_buffer.pop();

        if (pkt.insts.empty()) {
            pkt.base_pc = inst.pc;
            m_state[F3].vpc = inst.pc;
        }

        FetchInst finst;
        finst.valid    = true;
        finst.pc       = inst.pc;
        finst.insn     = inst.insn;
        finst.fetch_id = inst.fetch_id;
        finst.size     = k_inst_bytes;

        pkt.insts.push_back(finst);

        PredecodeInfo& pd = pkt.predecode[i];
        DecodeUnit::predecode_one(finst.insn, &pd);
        pd.slot = i;
        pkt.valid_mask |= (1u << i);
        // fetch3 根据实际取指重新预测结果
        if (pd.is_branch) {
            pkt.br_mask |= (1u << i);
            m_f3_pred[i] = m_bpu.f3_pred(finst.pc);
            if (m_f3_pred[i].valid) {
                if (m_f3_pred[i].final_pred != m_f3_bpred.pred[i].taken) {
                    m_redirect_pc[RES_F3].valid = true;
                    /* 预测结果与 f2 不符，应该先清除 f2 并重新取指*/
                    m_state[F2].valid = false;
                    m_epoch++;
                }
                if (m_f3_pred[i].final_pred) {
                    m_redirect_pc[RES_F3].pc = m_f3_bpred.pred[i].target;
                }
            }
            break;
            LOG_LEVEL1("F3 Predict", "branch mask:0x%x", pkt.br_mask);
        }
    }

    // 若本轮一个指令都没解析出来，则无法前进，等待更多字节
    if (pkt.insts.empty()) {
        m_f3_pkt.clear();
        return;
    }

    m_f3_pkt.valid = true;
    m_f3_pkt.data  = std::move(pkt);

    const auto &last = m_f3_pkt.data.insts.back();
    m_redirect_pc[RES_F3].valid = true;
    m_redirect_pc[RES_F3].pc = last.pc + last.size;
    m_state[F4].valid = true;
    m_run[F3] = true;
}

void FetchUnit::fetch4()
{
    if (!m_f3_pkt.valid || !m_state[F4].valid) {
        m_f4_pkt.clear();
        return;
    }
    m_state[F4].vpc = m_state[F3].vpc;

    bool pred_taken = false;
    for (u32 i = 0; i < m_fetch_width; ++i) {
        if (m_f3_pkt.data.br_mask & (1u << i) && m_f3_pred[i].valid) {
            auto &pred = m_f3_pred[i];
            if (pred.valid) {
                m_redirect_pc[RES_F4].valid = true;
                if (pred.final_pred) {
                    m_redirect_pc[RES_F4].pc = m_f3_bpred.pred[i].target;
                }
            }
            // TODO: update RAS && bpred etc.
            break;
        }
    }
    pc_t fall_addr = (m_state[F4].vpc / m_bank_size) * m_bank_size + m_bank_size;

    m_redirect_pc[RES_F4].pc = pred_taken ? m_redirect_pc[RES_F4].pc : fall_addr;
    m_f4_pkt.valid = true;
    m_f4_pkt.data = m_f3_pkt.data;
    m_state[F5].valid = true;
    m_run[F4] = true;
}
void FetchUnit::fetch5()
{
    if (!m_f4_pkt.valid || !m_state[F5].valid) {
        return;
    }
    if (m_fetch_decode_q.full()) {
        if (m_stats) {
            ++m_stats->frontend.decode_queue_full_cycles;
        }
        return;
    }
    m_f5_pkt.valid = true;
    m_f5_pkt.data = m_f4_pkt.data;
    if (m_fetch_decode_q.write(m_f5_pkt.data) && m_stats) {
        ++m_stats->frontend.fetch_packets;
        for (const auto& inst : m_f5_pkt.data.insts) {
            if (inst.valid) {
                ++m_stats->frontend.fetch_insts;
            }
        }
    }
    m_run[F5] = true;
}

FetchUnit::~FetchUnit() { }


pc_t FetchUnit::next_pc()
{
    m_pc = align_bank(m_pc) + m_bank_size;
    for (int i = 0; i <= RESET_PC; ++i) {
        if (m_redirect_pc[i].valid) {
            m_pc = m_redirect_pc[i].pc;
            m_redirect_pc[i].valid = false;
        }
        m_redirect_pc[i].pc = 0;
    }
    return m_pc;
}

u32 FetchUnit::align_bank(pc_t pc) const
{
    return (pc / m_bank_size) * m_bank_size;
}

u32 FetchUnit::bank_idx(pc_t pc) const
{
    return (pc - m_icache.align_line(pc)) / m_bank_size;
}

u32 FetchUnit::group_idx(pc_t pc) const
{
    return (pc - align_group(pc)) / m_group_size;
}

u32 FetchUnit::align_group(pc_t pc) const
{
    return (pc / m_group_size) * m_group_size;
}

void FetchUnit::flush()
{
    if (!m_flush_event.valid) {
        return;
    }
    switch (m_flush_event.type) {
    case FlushType::EXCEPTION:
    case FlushType::DEBUG:
    case FlushType::INTERRUPT:
    case FlushType::MEMORY_ORDER:
    case FlushType::LOAD_REPLAY:
    case FlushType::FENCE: {
        assert(false);
    }
    case FlushType::BRANCH_MISPRED: {
        if (m_stats) {
            m_stats->frontend.flushed_insts += m_inst_buffer.size();
            auto count_pkt = [](const pipe_reg_t<FetchPacket>& pkt) {
                u64 count = 0;
                if (!pkt.valid) {
                    return count;
                }
                for (const auto& inst : pkt.data.insts) {
                    if (inst.valid) {
                        ++count;
                    }
                }
                return count;
            };
            m_stats->frontend.flushed_insts += count_pkt(m_f3_pkt);
            m_stats->frontend.flushed_insts += count_pkt(m_f4_pkt);
            m_stats->frontend.flushed_insts += count_pkt(m_f5_pkt);
        }
        m_redirect_pc[RESET_PC].valid = true;
        m_redirect_pc[RESET_PC].pc = m_flush_event.redirect_pc;
        m_epoch++;
        for (u32 i = 0; i < MAX_STATES; ++i) {
            m_state[i].clear();
        }
        m_state[F0].valid = true;
        m_inst_buffer = std::queue<F2Inst>();
        m_f3_pkt.clear();
        m_f4_pkt.clear();
        m_f5_pkt.clear();
        m_fetch_decode_q.for_each_mut([&](FetchPacket& pkt) {
            for (size_t i = 0; i < pkt.insts.size(); ++i) {
                auto& fi = pkt.insts[i];
                if (!fi.valid)
                    continue;
                if (fi.fetch_id > m_flush_event.fetch_id) {
                    if (m_stats) {
                        ++m_stats->frontend.flushed_insts;
                    }
                    fi.valid = false;
                    pkt.valid_mask &= ~(1u << i);
                    pkt.br_mask    &= ~(1u << i);
                }
            }
            // 返回 false => 从队列删掉空包
            return pkt.valid_mask != 0;
        });
    } break;
    default:
        assert(false);
    }
}

/* -------------------- Debug / state dump -------------------- */

void FetchUnit::dump_state() const
{
    if (m_flush_event.valid) {
        auto evt = m_flush_event;
        auto type = FlushTypeStr[static_cast<int>(evt.type)];
        auto source = FlushSourceStr[static_cast<int>(evt.source)];
        auto scope = FlushScopeStr[static_cast<int>(evt.scope)];
        auto fetch_id = evt.fetch_id;
        auto inst_pc = evt.inst_pc;
        auto r_pc = evt.redirect_pc;
        auto rid = evt.rid;
        std::string id_pre = rid.idx == 0 ? "++" : "--";
        LOG_LEVEL1("FetchUnit Flush Event", "type: %s, source: %s, scope: %s, rid: %s%d, rdirect pc:0x%lx, "
                   "fetch_id: %d, inst_pc:0x%lx",
                   type, source, scope, id_pre.c_str(), rid.idx, r_pc, fetch_id, inst_pc);
    }
    if (m_state[F0].valid) {
        pc_t line_addr = m_icache.align_line(m_state[F0].vpc);
        pc_t buffer_addr = m_line_buffer.line_addr;
        bool line_valid = m_line_buffer.valid;
        pc_t vpc = m_state[F0].vpc;
        LOG_LEVEL1("Fetch0", "vpc:0x%lx,line vld:%d, buffer addr:0x%lx, req line:0x%lx",
            vpc, line_valid, buffer_addr, line_addr);
    }
    if (m_run[F1]) {
        auto &pred = m_f1_pred.pred;
        LOG_LEVEL1("Fetch1", "vpc:0x%lx,ghr:0x%lx, redirect:(%d-%lx),"
                    "pred:[v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx]",
                    m_state[F1].vpc, m_f1_pred.ghr_snapshot, m_redirect_pc[RES_F1].valid, m_redirect_pc[RES_F1].pc,
                    pred[0].valid, pred[0].taken, pred[0].target, pred[1].valid, pred[1].taken, pred[1].target,
                    pred[2].valid, pred[2].taken, pred[2].target, pred[3].valid, pred[3].taken, pred[3].target);
    }

    if (m_run[F2]) {
        auto &pred = m_f2_pred.pred;
        LOG_LEVEL1("Fetch2", "vpc:0x%lx,ghr:0x%lx,redirect:(%d-%lx), "
                    "pred:[v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx]",
                    m_state[F2].vpc, m_f2_pred.ghr_snapshot, m_redirect_pc[RES_F2].valid, m_redirect_pc[RES_F2].pc,
                    pred[0].valid, pred[0].taken, pred[0].target, pred[1].valid, pred[1].taken, pred[1].target,
                    pred[2].valid, pred[2].taken, pred[2].target, pred[3].valid, pred[3].taken, pred[3].target);
    }
    if (m_run[F3]) {
        auto &pkt = m_f3_pkt.data;
        auto r_v = m_redirect_pc[RES_F3].valid;
        auto r_pc = m_redirect_pc[RES_F3].pc;
        LOG_LEVEL1("Fetch3", "vpc:0x%lx, redirect:(%d-%lx), base pc:0x%lx, valid mask:0x%x, br mask:0x%x",
                    m_state[F3].vpc, r_v, r_pc, pkt.base_pc, pkt.valid_mask, pkt.br_mask);

    }
    if (m_run[F4]) {
        auto r_v = m_redirect_pc[RES_F4].valid;
        auto r_pc = m_redirect_pc[RES_F4].pc;
        auto &pkt = m_f4_pkt.data;
        LOG_LEVEL1("Fetch4", "vpc:0x%lx, redirect:(%d-%lx), base pc:0x%lx, valid mask:0x%x, br mask:0x%x",
                    m_state[F4].vpc, r_v, r_pc, pkt.base_pc, pkt.valid_mask, pkt.br_mask);
    }
    if (m_run[F5]) {
        auto &pkt = m_f5_pkt.data;
        LOG_LEVEL1("Fetch5", "base pc:0x%lx, valid mask:0x%x, br mask:0x%x",
                    pkt.base_pc, pkt.valid_mask, pkt.br_mask);
    }
    return;
}
