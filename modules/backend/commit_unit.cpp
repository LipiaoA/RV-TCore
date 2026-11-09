#include "commit_unit.h"

#include <algorithm>
#include <cstddef>

#include "instr_trace.h"
#include "rob/rob.h"
#include "rename/rename_unit.h"
#include "execute/lsu/lsu.h"
#include "verify/ref_model_checker.h"
#include "isa/decode.h"

namespace {
bool same_rob_id(const ROBID& lhs, const ROBID& rhs)
{
    return lhs.idx == rhs.idx && lhs.direct == rhs.direct;
}
}

CommitUnit::CommitUnit(const CommitConfig& conf, const CommitDeps& deps)
    : ICommit()
    , m_commit_width(conf.commit_width)
    , m_resolve_width(conf.resolve_width)
    , m_rob(*deps.rob)
    , m_rename(*deps.rename)
    , m_reg_req_q(*deps.reg_req_q)
    , m_vec_reg_req_q(*deps.vec_reg_req_q)
    , m_tile_reg_req_q(*deps.tile_reg_req_q)
    , m_flush_req_q(*deps.flush_req_q)
    , m_flush_event(*deps.flush_event)
    , m_inst_commit_q(*deps.inst_commit_q)
    , m_crat(*deps.crat)
    , m_vcrat(*deps.vcrat)
    , m_tcrat(*deps.tcrat)
    , m_checkpoints(*deps.checkpoints)
    , m_lsu(*deps.lsu)
    , m_stats(*deps.perf_stats)
{}

void CommitUnit::build()
{
    InstrTrace::instance().open("instr_trace.log");
}

namespace {

RetireMemInfo retired_ls_info(const MInstPtr& inst, LSU& lsu)
{
    RetireMemInfo mem {};
    if (!inst) {
        return mem;
    }

    if (inst->type == InstType::LOAD) {
        if (LQEntry* ld = lsu.find_load_by_rid(inst->rid)) {
            mem.valid = ld->addr_ready;
            mem.addr = ld->addr;
            mem.width = inst->ls_width;
            mem.data = inst->rd.valid ? inst->rd.val : ld->data;
        }
        return mem;
    }

    if (inst->type == InstType::STA || inst->type == InstType::STD || inst->type == InstType::STORE) {
        if (SQEntry* st = lsu.find_store_by_rid(inst->rid)) {
            mem.valid = st->addr_ready && st->data_ready;
            mem.addr = st->addr;
            mem.data = st->data;
            mem.width = st->ls_width != 0 ? st->ls_width : inst->ls_width;
        }
    }
    return mem;
}

}

void CommitUnit::tick() {
    flush();
    inst_commit();
    inst_resolve();
}

void CommitUnit::inst_resolve() {
    for (u32 i = 0; i < m_resolve_width; i++) {
        MInstPtr inst;

        if (!m_pending_writebacks.empty()) {
            inst = m_pending_writebacks.front();
            m_pending_writebacks.pop_front();
        } else {
            bool ret = m_inst_commit_q.read(inst);
            if (!ret) {
                break;
            }
        }

        if (!inst || !inst->valid || should_flush(inst->rid)) {
            continue;
        }
        if (inst->state == InstState::ISSUED) {
            RobEntry* entry = m_rob.get(inst->rid);
            if (!entry || entry->inst != inst) {
                continue;
            }
            if (!write_back(inst)) {
                m_pending_writebacks.push_front(inst);
                break;
            }
            inst->state = InstState::EXECUTED;
            m_rob.mark_ready(inst->rid, inst->rd.valid ? inst->rd.val : 0);
            if (m_stats.perf()) {
                ++m_stats.commit.resolved_insts;
            }
            LOG_LEVEL1("CommitUnit", "pc: 0x%lx, inst: %s type:%s resolved, rid:%s%u", inst->pc,
                        inst->inst_name.c_str(), inst_type_str(inst->type),
                        inst->rid.direct ? "--" : "++", inst->rid.idx);
        }
    }
}

void CommitUnit::inst_commit() {
    if (m_rob.empty()) {
        if (m_stats.perf()) {
            ++m_stats.commit.rob_empty_cycles;
        }
        return;
    }
    for (u32 i = 0; i < m_commit_width; i++) {
        auto entry = m_rob.head();
        if (entry && entry->valid && entry->inst && entry->inst->valid
            && entry->inst->state == InstState::EXECUTED) {
            if (entry->inst->type == InstType::STORE &&
                !m_lsu.store_commit_ready(entry->inst->rid)) {
                if (i == 0 && m_stats.perf()) {
                    ++m_stats.commit.head_not_ready_cycles;
                }
                break;
            }
            if (entry->inst->fetch_fault) {
                FlushEvent event;
                event.valid = true;
                event.type = FlushType::EXCEPTION;
                event.source = FlushSource::IFETCH;
                event.scope = FlushScope::ALL;
                event.has_exception = true;
                event.exception_cause = entry->inst->exception_cause;
                event.exception_tval = entry->inst->exception_tval;
                event.inst_pc = entry->inst->pc;
                event.fetch_id = entry->inst->fetch_id;
                event.redirect_pc = entry->inst->pc;
                m_flush_req_q.write(event);
                LOG_LEVEL1("CommitUnit", "fetch fault exception pc:0x%lx cause:%lu tval:0x%lx",
                           entry->inst->pc, entry->inst->exception_cause, entry->inst->exception_tval);
            }
            entry->inst->state = InstState::RETIRED;
            if (m_stats.perf()) {
                ++m_stats.commit.retired_insts;
            }
            auto committed = m_rob.commit_head();
            if (!committed) {
                break;
            }
            if (!committed->inst->fetch_fault) {
                retire_inst(committed->inst);
            } else {
                // gererate exception response
                assert(false);
            }

            if (!committed->inst->fetch_fault) {
                std::string rid_str = committed->inst->rid.direct ? "--" : "++";
                u32 r_idx = committed->inst->rid.idx;
                LOG_LEVEL1("CommitUnit", "pc: 0x%lx commit inst: %s retired, rid: %s%u",
                    committed->inst->pc, committed->inst->inst_name.c_str(), rid_str.c_str(), r_idx);
                u64 result = committed->inst->rd.valid ? committed->inst->rd.val : 0;
                InstrTrace::instance().record_retire(committed->inst->pc, result);
                RetireMemInfo mem_info = retired_ls_info(committed->inst, m_lsu);
                if (!VeriRef::instance().check_retire(*committed->inst, &mem_info)) {
                    LOG_LEVEL1("CommitUnit", "ERROR! Ref Model Mismatch At PC: 0x%lx inst: %s",
                               committed->inst->pc, committed->inst->inst_name.c_str());
                    FlushEvent event;
                    event.valid = true;
                    event.type = FlushType::EXCEPTION;
                    event.source = FlushSource::COMMIT;
                    event.scope = FlushScope::ALL;
                    m_flush_req_q.write(event);
                    break;
                }
            }
            if (isa::is_ecall(committed->inst->opcode)) {
                LOG_LEVEL1("CommitUnit", "Ecall PC: 0x%lx", committed->inst->pc);
                FlushEvent event;
                event.valid = true;
                event.type = FlushType::STOP;
                event.source = FlushSource::COMMIT;
                event.scope = FlushScope::ALL;
                m_flush_req_q.write(event);
                break;
            }
        } else {
            if (i == 0 && m_stats.perf()) {
                ++m_stats.commit.head_not_ready_cycles;
            }
            break;
        }
    }
    return;
}

bool CommitUnit::write_back(const MInstPtr inst)
{
    if (inst->type == InstType::VECTOR && inst->vd.valid) {
        VecRegReq req;
        req.valid = true;
        req.write = true;
        req.ptag = inst->vd.ptag;
        req.atag = inst->vd.atag;
        req.val = inst->vd_payload;
        req.rid = inst->rid;
        req.fu = FuType::VPU;
        return m_vec_reg_req_q.write(req);
    }
    if (inst->type == InstType::TENSOR && inst->td.valid) {
        TileRegReq req;
        req.valid = true;
        req.write = true;
        req.ptag = inst->td.ptag;
        req.atag = inst->td.atag;
        req.val = inst->td_payload;
        req.rid = inst->rid;
        req.fu = FuType::TPU;
        return m_tile_reg_req_q.write(req);
    }
    if (!inst->rd.valid) {
        return true;
    }
    RegReq req;
    req.valid = true;
    req.ptag = inst->rd.ptag;
    req.write = true;
    req.val = inst->rd.val;
    req.rid = inst->rid;
    req.rtyp = RegType::GPR;
    return m_reg_req_q.write(req);
}

void CommitUnit::retire_inst(const MInstPtr inst)
{
    if (!inst || !inst->valid) {
        return;
    }

    if ((inst->type == InstType::STA || inst->type == InstType::STD || inst->type == InstType::STORE)) {
        m_lsu.commit_store(inst->rid);
    }
    if (inst->type == InstType::LOAD) {
        m_lsu.retire_load(inst->rid);
    }

    if (inst->rd.valid && inst->rd.rtyp == RegType::GPR && inst->rd.atag > 0
        && static_cast<std::size_t>(inst->rd.atag) < m_crat.size()) {
        m_crat[inst->rd.atag] = inst->rd.ptag;
        if (inst->rd.o_ptag > 0 && inst->rd.o_ptag != inst->rd.ptag) {
            m_rename.recycle_phys(inst->rd.o_ptag);
        }
    }
    if (inst->type == InstType::VECTOR && inst->vd.valid
        && static_cast<std::size_t>(inst->vd.atag) < m_vcrat.size()) {
        m_vcrat[inst->vd.atag] = inst->vd.ptag;
        if (inst->vd.o_ptag >= 0 && inst->vd.o_ptag != inst->vd.ptag) {
            m_rename.recycle_vec_phys(inst->vd.o_ptag, inst->vd.rgrp);
        }
    }
    if (inst->type == InstType::TENSOR && inst->td.valid
        && static_cast<std::size_t>(inst->td.atag) < m_tcrat.size()) {
        m_tcrat[inst->td.atag] = inst->td.ptag;
        if (inst->td.o_ptag >= 0 && inst->td.o_ptag != inst->td.ptag) {
            m_rename.recycle_tile_phys(inst->td.o_ptag);
        }
    }

    m_checkpoints.erase(
        std::remove_if(m_checkpoints.begin(), m_checkpoints.end(),
            [&](const RatCheckpoint& cp) {
                return same_rob_id(cp.rid, inst->rid)
                    || (inst->c_id != 0 && cp.c_id == inst->c_id);
            }),
        m_checkpoints.end());
}

void CommitUnit::commit()
{
    InstrTrace::instance().flush();
}

void CommitUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    m_pending_writebacks.erase(
        std::remove_if(m_pending_writebacks.begin(), m_pending_writebacks.end(),
            [&](const MInstPtr& inst) {
                bool flushed = inst && should_flush(inst->rid);
                if (flushed) {
                    inst->valid = false;
                    inst->state = InstState::FLUSHED;
                }
                return flushed;
            }),
        m_pending_writebacks.end());
    m_inst_commit_q.flush_if([&](const MInstPtr& inst) {
        bool flushed = inst && should_flush(inst->rid);
        if (flushed) {
            inst->valid = false;
            inst->state = InstState::FLUSHED;
        }
        return flushed;
    });
}

bool CommitUnit::should_flush(const ROBID& rid) const
{
    if (!m_flush_event.valid) {
        return false;
    }
    switch (m_flush_event.scope) {
    case FlushScope::ALL:
        return true;
    case FlushScope::YOUNGER_THAN:
        return m_rob.is_older_than(m_flush_event.rid, rid);
    case FlushScope::FROM_RID:
        return same_rob_id(m_flush_event.rid, rid)
            || m_rob.is_older_than(m_flush_event.rid, rid);
    case FlushScope::NONE:
        return false;
    default:
        return false;
    }
}

void CommitUnit::reset() {
    m_pending_writebacks.clear();
    m_inst_commit_q.clear();
    m_flush_req_q.clear();
}
