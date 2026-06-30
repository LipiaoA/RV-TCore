
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
#include "isa/decode.h"

extern u64 g_cycles;
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
    , m_bank_count(conf.bank_count)
    , m_group_count(conf.group_count)
    , m_cache_line_size(conf.cache_line_size)
{
    m_line_buffer.clear();
    m_f2_line.clear();
    m_f3_pkt.clear();
    m_f4_pkt.clear();
    m_f5_pkt.clear();

    m_pc          = 0;
    m_squash_id   = 0;
    memset(m_run, 0, sizeof(m_run));
    for (auto& bundle : m_state) {
        bundle.clear();
    }
    for (auto& redirect : m_redirect_pc) {
        redirect.valid = false;
        redirect.pc = 0;
    }
    m_inflight_valid = false;
    m_inflight.clear();
    m_pending_fetch_fault.valid = false;
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
    clear_inflight();
    m_pending_fetch_fault.valid = false;
    m_line_buffer.clear();
    m_state[F0].valid = true;
}

void FetchUnit::preprocess()
{
    memset(m_run, 0, sizeof(m_run));
}

bool FetchUnit::has_redirect() const
{
    for (const auto& redirect : m_redirect_pc) {
        if (redirect.valid) {
            return true;
        }
    }
    return false;
}

bool FetchUnit::fetch_stalled() const
{
    return m_inflight_valid;
}

void FetchUnit::clear_inflight()
{
    m_inflight_valid = false;
    m_inflight.clear();
}

u64 FetchUnit::fault_cause(const IFetchResp& resp) const
{
    if (resp.page_fault) {
        return EXC_INSTR_PAGE_FAULT;
    }
    if (resp.access_fault) {
        return EXC_INSTR_ACCESS_FAULT;
    }
    return 0;
}

void FetchUnit::emit_fault_packet()
{
    FetchPacket pkt;
    pkt.valid = true;
    pkt.fetch_fault = true;
    pkt.fault_pc = m_pending_fetch_fault.fault_pc;
    pkt.fault_fetch_id = m_pending_fetch_fault.fetch_id;
    pkt.exception_cause = m_pending_fetch_fault.cause;
    pkt.exception_tval = m_pending_fetch_fault.tval;
    pkt.base_pc = m_pending_fetch_fault.fault_pc;
    m_state[F3].vpc = m_pending_fetch_fault.fault_pc;
    m_f3_pkt.valid = true;
    m_f3_pkt.data = std::move(pkt);
    m_pending_fetch_fault.valid = false;
    m_state[F4].valid = true;
    m_run[F3] = true;
}

void FetchUnit::fetch0()
{
    if (!m_state[F0].valid) {
        return;
    }

    // I$ 未返回前禁止 PC 漂移：不推进 F0/F1，不发新请求
    if (fetch_stalled() && !has_redirect()) {
        m_run[F0] = true;
        return;
    }

    if (has_redirect()) {
        clear_inflight();
    }

    FetchBundle bundle {};
    bundle.valid = true;
    bundle.vpc = next_pc();
    bundle.line_addr = m_icache.align_line(bundle.vpc);
    bundle.epoch = m_epoch;
    bundle.icache_sent = false;

    const bool line_cached = m_line_buffer.valid
        && m_line_buffer.line_addr == bundle.line_addr
        && m_line_buffer.epoch == bundle.epoch;

    if (line_cached) {
        m_state[F0] = bundle;
        m_state[F1].valid = true;
        m_run[F0] = true;
        return;
    }

    IFetchReq ireq {};
    ireq.valid = true;
    ireq.pc = bundle.vpc;
    ireq.line_addr = bundle.line_addr;
    ireq.epoch = bundle.epoch;

    m_line_buffer.clear();
    bool accepted = m_icache.request(ireq);
    LOG_LEVEL1("F0 Send Request", "req vpc:0x%lx, line_addr:0x%lx, epoch:%d, accepted:%d",
                bundle.vpc, bundle.line_addr, bundle.epoch, accepted);
    if (!accepted) {
        m_run[F0] = true;
        return;
    }

    bundle.icache_sent = true;
    m_inflight = bundle;
    m_inflight_valid = true;
    m_state[F0] = bundle;
    m_state[F1].valid = true;
    m_run[F0] = true;
}

void FetchUnit::fetch1()
{
    if (!m_state[F1].valid) {
        m_redirect_pc[RES_F1].valid = false;
        return;
    }

    m_state[F1] = m_state[F0];
    m_f1_pred = m_bpu.f1_pred(m_state[F1].vpc);
    for (u32 i = 0; i < m_fetch_width; ++i) {
        if (!m_f1_pred.pred[i].valid)
            continue;
        const bool unconditional = m_f1_pred.pred[i].target != static_cast<pc_t>(-1ull);
        if (m_f1_pred.pred[i].taken || unconditional) {
            m_redirect_pc[RES_F1].valid = true;
            m_redirect_pc[RES_F1].pc = m_f1_pred.pred[i].target;
            break;
        }
    }
    // TODO: itlb request

    m_state[F2] = m_state[F1];
    m_state[F2].valid = true;
    m_state[F1].valid = false;
    m_run[F1] = true;
}

void FetchUnit::fetch2()
{
    if (!m_state[F2].valid) {
        return;
    }
    if (m_inst_buffer.size() + m_fetch_width > MAX_INST_COUNT) {
        return;
    }

    const FetchBundle &b = m_state[F2];
    m_f2_pred = m_bpu.f2_pred(b.vpc);

    const bool line_match = m_line_buffer.valid
        && m_line_buffer.line_addr == b.line_addr
        && m_line_buffer.epoch == b.epoch;

    if (line_match || m_icache.line_ready(b.vpc)) {
        IFetchResp resp {};
        if (!line_match) {
            resp = m_icache.read_line(b.vpc);
            if (!resp.valid || resp.epoch != b.epoch) {
                if (m_stats) {
                    ++m_stats->frontend.icache_wait_cycles;
                }
                return;
            }
        }

        if (!line_match && (resp.page_fault || resp.access_fault)) {
            m_pending_fetch_fault.valid = true;
            m_pending_fetch_fault.fault_pc = b.vpc;
            m_pending_fetch_fault.fetch_id = m_fetch_id++;
            m_pending_fetch_fault.cause = fault_cause(resp);
            m_pending_fetch_fault.tval = b.vpc;
            LOG_LEVEL1("Fetch2", "I$ fault vpc:0x%lx, cause:%lu, fetch_id:%lu",
                       b.vpc, m_pending_fetch_fault.cause, m_pending_fetch_fault.fetch_id);
            if (m_inflight_valid
                && m_inflight.line_addr == b.line_addr
                && m_inflight.epoch == b.epoch) {
                clear_inflight();
            }
            m_state[F2].valid = false;
            m_state[F3].valid = true;
            m_run[F2] = true;
            return;
        }

        if (!line_match) {
            memcpy(m_line_buffer.data, resp.data, sizeof(m_line_buffer.data));
            m_line_buffer.valid = true;
            m_line_buffer.line_addr = resp.line_addr;
            m_line_buffer.epoch = b.epoch;
            m_line_buffer.bmask = 0;
        }

        auto &line = m_line_buffer.data;
        pc_t cur_pc = b.vpc;
        u32 gstart_offset = align_group(cur_pc) % m_cache_line_size;
        u32 start_offset = cur_pc % m_cache_line_size;
        u32 end_offset = gstart_offset + m_group_size;
        u32 oft = start_offset;
        while (oft < end_offset) {
            F2Inst inst {};
            for (int j = 0; j < 4; ++j) {
                inst.insn |= static_cast<u32>(line[oft + j]) << (8 * j);
            }
            inst.pc = cur_pc;
            inst.fetch_id = m_fetch_id++;
            m_inst_buffer.push(inst);

            cur_pc += 4;
            oft += 4;
        }
        m_line_buffer.clear();

        if (m_inflight_valid
            && m_inflight.line_addr == b.line_addr
            && m_inflight.epoch == b.epoch) {
            clear_inflight();
        }
        m_state[F2].valid = false;
    } else {
        if (m_stats) {
            ++m_stats->frontend.icache_wait_cycles;
        }
        return;
    }

    for (u32 i = 0; i < m_fetch_width; ++i) {
        m_f2_pred.pred[i].target = m_f1_pred.pred[i].target;
        if (m_f2_pred.pred[i].valid) {
            if (m_f2_pred.pred[i].taken) {
                m_redirect_pc[RES_F2].valid = true;
                m_redirect_pc[RES_F2].pc = m_f2_pred.pred[i].target;
            }
            if (m_f2_pred.pred[i].taken != m_f1_pred.pred[i].taken) {
                m_state[F2].valid = false;
                m_epoch++;
            }
            break;
        }
    }
    if (m_redirect_pc[RES_F2].valid) {
        m_state[F1].valid = false;
        m_state[F2].valid = false;
    }
    m_state[F3].valid = true;
    m_run[F2] = true;
}

void FetchUnit::fetch3()
{
    // - 每条指令固定 4 字节，不支持 2 字节压缩指令。
    // - 若当前剩余字节不足以组成一条完整指令，则停在这里，等待下一条 cache line 合并后再解析。

    if (m_f3_pkt.valid && m_state[F4].valid) {
        // F4 尚未消费当前包，不能覆盖。
        return;
    }

    if (m_pending_fetch_fault.valid) {
        emit_fault_packet();
        return;
    }

    // 如果没有可组包的指令，清空本级输出，避免旧包重复向后传递。
    if (m_inst_buffer.empty()) {
        m_f3_pkt.clear();
        m_state[F4].valid = false;
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
        isa::predecode(finst.insn, &pd);
        pd.slot = i;
        pkt.valid_mask |= (1u << i);
        // fetch3 根据实际取指重新预测结果
        if (pd.is_branch) {
            pkt.br_mask |= (1u << i);
            m_f3_pred[i] = m_bpu.f3_pred(finst.pc);
            pkt.pred_taken = m_f3_pred[i].valid ? m_f3_pred[i].final_pred : m_f3_bpred.pred[i].taken;
            pkt.pred_target = m_f3_bpred.pred[i].target;
            if (m_f3_pred[i].valid) {
                if (m_f3_pred[i].final_pred != m_f3_bpred.pred[i].taken) {
                    m_redirect_pc[RES_F3].valid = m_f3_pred[i].final_pred;
                    m_redirect_pc[RES_F3].pc = m_f3_bpred.pred[i].target;
                    m_inst_buffer = std::queue<F2Inst>();
                    m_epoch++;
                }
                if (m_f3_pred[i].final_pred) {
                    m_redirect_pc[RES_F3].valid = true;
                    m_redirect_pc[RES_F3].pc = m_f3_bpred.pred[i].target;
                    m_state[F2].valid = false;
                    m_inst_buffer = std::queue<F2Inst>();
                    m_state[F1].valid = false;
                }
                // not-taken fall-through：不写 redirect，由 fetch0/next_pc() 处理
            }
            LOG_LEVEL1("F3 Predict", "branch mask:0x%x", pkt.br_mask);
            break;
        }
    }

    // 若本轮一个指令都没解析出来，则无法前进，等待更多字节
    if (pkt.insts.empty()) {
        m_f3_pkt.clear();
        return;
    }

    m_f3_pkt.valid = true;
    m_f3_pkt.data  = std::move(pkt);

    m_state[F4].valid = true;
    m_run[F3] = true;
}

void FetchUnit::fetch4()
{
    if (m_f4_pkt.valid && m_state[F5].valid) {
        // F5 还没有把包写入 decode 队列，保持当前 F4 包不被覆盖。
        return;
    }
    if (!m_f3_pkt.valid || !m_state[F4].valid) {
        m_f4_pkt.clear();
        return;
    }
    m_state[F4].vpc = m_state[F3].vpc;

    for (u32 i = 0; i < m_fetch_width; ++i) {
        if (m_f3_pkt.data.br_mask & (1u << i) && m_f3_pred[i].valid) {
            auto &pred = m_f3_pred[i];
            if (pred.valid && pred.final_pred) {
                m_redirect_pc[RES_F4].valid = true;
                m_redirect_pc[RES_F4].pc = m_f3_bpred.pred[i].target;
            }
            // TODO: update RAS && bpred etc.
            break;
        }
    }
    m_f4_pkt.valid = true;
    m_f4_pkt.data = m_f3_pkt.data;
    m_f3_pkt.clear();
    m_state[F4].valid = false;
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
    bool wrote = m_fetch_decode_q.write(m_f5_pkt.data);
    if (!wrote) {
        return;
    }
    if (m_stats) {
        ++m_stats->frontend.fetch_packets;
        for (const auto& inst : m_f5_pkt.data.insts) {
            if (inst.valid) {
                ++m_stats->frontend.fetch_insts;
            }
        }
    }
    m_f4_pkt.clear();
    m_state[F5].valid = false;
    m_run[F5] = true;
}

FetchUnit::~FetchUnit() { }


pc_t FetchUnit::next_pc()
{
    // 控制流 redirect（taken / mispred / flush）：优先级 RESET_PC > F4 > … > F1
    m_next_fetch_pc = align_group(m_pc) + m_group_size;

    for (int i = RESET_PC; i >= RES_F1; --i) {
        if (!m_redirect_pc[i].valid)
            continue;
        m_pc = m_redirect_pc[i].pc;
        m_redirect_pc[i].valid = false;
        m_redirect_pc[i].pc = 0;
        m_next_fetch_pc = m_pc;
        return m_pc;
    }

    m_pc = m_next_fetch_pc;
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
    case FlushType::FENCE:
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
        clear_inflight();
        m_line_buffer.clear();
        if (m_pending_fetch_fault.valid
            && m_pending_fetch_fault.fetch_id > m_flush_event.fetch_id) {
            m_pending_fetch_fault.valid = false;
        }
        for (auto& redirect : m_redirect_pc) {
            redirect.valid = false;
            redirect.pc = 0;
        }
        m_redirect_pc[RESET_PC].valid = true;
        m_redirect_pc[RESET_PC].pc = m_flush_event.redirect_pc;
        for (u32 i = 0; i < MAX_STATES; ++i) {
            m_state[i].clear();
        }
        m_state[F0].valid = true;
        m_inst_buffer = std::queue<F2Inst>();
        m_pending_fetch_fault.valid = false;
        m_f3_pkt.clear();
        m_f4_pkt.clear();
        m_f5_pkt.clear();
        m_fetch_decode_q.for_each_mut([&](FetchPacket& pkt) {
            if (pkt.fetch_fault) {
                if (pkt.fault_fetch_id > m_flush_event.fetch_id) {
                    if (m_stats) {
                        ++m_stats->frontend.flushed_insts;
                    }
                    pkt.fetch_fault = false;
                    return false;
                }
                return true;
            }
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
            return pkt.fetch_fault || pkt.valid_mask != 0;
        });
    } break;
    default:
        assert(false);
    }
}

void FetchUnit::reset()
{
    m_pc = 0;
    m_squash_id = 0;
    m_fetch_id = 1;
    m_epoch = 0;
    clear_inflight();
    m_pending_fetch_fault.valid = false;

    m_line_buffer.clear();
    m_inst_buffer = std::queue<F2Inst>();

    m_f2_line.clear();
    m_f3_pkt.clear();
    m_f4_pkt.clear();
    m_f5_pkt.clear();

    memset(m_run, 0, sizeof(m_run));

    for (auto& bundle : m_state) {
        bundle.clear();
    }
    for (auto& redirect : m_redirect_pc) {
        redirect.valid = false;
        redirect.pc = 0;
    }

    memset(&m_f1_pred, 0, sizeof(m_f1_pred));
    memset(&m_f2_pred, 0, sizeof(m_f2_pred));
    memset(&m_f3_bpred, 0, sizeof(m_f3_bpred));
    memset(m_f3_pred, 0, sizeof(m_f3_pred));

    m_fetch_decode_q.clear();
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
        LOG_LEVEL1("Fetch0", "vpc:0x%lx,line vld:%d, buffer addr:0x%lx, req line:0x%lx, inflight:%d",
            vpc, line_valid, buffer_addr, line_addr, m_inflight_valid);
    }
    if (m_run[F1]) {
        auto &pred = m_f1_pred.pred;
        LOG_LEVEL1("Fetch1", "vpc:0x%lx,line:0x%lx,epoch:%d, redirect:(%d-%lx),"
                    "pred:[v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx]",
                    m_state[F1].vpc, m_state[F1].line_addr, m_state[F1].epoch,
                    m_redirect_pc[RES_F1].valid, m_redirect_pc[RES_F1].pc,
                    pred[0].valid, pred[0].taken, pred[0].target, pred[1].valid, pred[1].taken, pred[1].target,
                    pred[2].valid, pred[2].taken, pred[2].target, pred[3].valid, pred[3].taken, pred[3].target);
    }

    if (m_run[F2]) {
        auto &pred = m_f2_pred.pred;
        LOG_LEVEL1("Fetch2", "vpc:0x%lx,line:0x%lx,epoch:%d,ghr:0x%lx,redirect:(%d-%lx), "
                    "pred:[v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx | v%d-t%d-0x%lx]",
                    m_state[F2].vpc, m_state[F2].line_addr, m_state[F2].epoch,
                    m_f2_pred.ghr_snapshot, m_redirect_pc[RES_F2].valid, m_redirect_pc[RES_F2].pc,
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
