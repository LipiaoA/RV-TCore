#include "rename_unit.h"
#include "rob/rob.h"
#include "issue/issue_unit.h"
#include "isa/opcode.h"
#include "regfile_unit.h"

RenameUnit::RenameUnit(const RenameConfig& conf, const RenameDeps& deps)
    : IRename("rename")
    , m_decode_rename_q(*deps.decode_rename_q)
    , m_rename_issue_q(*deps.rename_issue_q)
    , m_rob(*deps.rob)
    , m_regfile(*deps.regfile)
    , m_crat(*deps.crat)
    , m_checkpoints(*deps.checkpoints)
    , m_flush_event(*deps.flush_event)
    , m_rename_width(conf.width)
    , m_prf_capacity(conf.prf_capacity)
{
    for (u32 p = 1; p < m_prf_capacity; ++p) {
        m_free_list.push_back(static_cast<i32>(p));
    }
    // 0 is x0, is zero register
    m_rat[0] = 0;
    m_checkpoint_id = 1;
}

void RenameUnit::tick() {
    flush();
    r2();
    r1();
    r0();
}

void RenameUnit::r0() {
    if (m_decode_rename_q.empty())
        return;
    size_t n = 0;
    MInstPtr inst;
    while (n < m_rename_width && m_decode_rename_q.read(inst)) {
        if (!inst->valid)
            continue;
        LOG_LEVEL1("RenameUnit R0", "r0 pc:0x%lx, inst: %s", inst->pc, inst->inst_name.c_str());
        m_r1_buf.push_back(inst);
        ++n;
    }
}

void RenameUnit::r1() {
    if (m_r1_buf.empty()) {
        return;
    }
    size_t n = 0;
    while (n < m_rename_width && !m_r1_buf.empty()) {
        MInstPtr inst = m_r1_buf.front();
        m_r1_buf.pop_front();

        int pidx = -1;
        if (inst->rs1.valid) {
            inst->rs1.ptag = m_rat[inst->rs1.atag];
            inst->rs1.ready = m_regfile.is_ready(inst->rs1.ptag);
        }
        if (inst->rs2.valid) {
            inst->rs2.ptag = m_rat[inst->rs2.atag];
            inst->rs2.ready = m_regfile.is_ready(inst->rs2.ptag);
        }
        if (inst->rd.valid) {
            if (m_free_list.empty()) {
                m_r1_buf.push_front(inst);
                break;
            }
            pidx = m_free_list.front();
            m_free_list.pop_front();
            inst->rd.ptag = pidx;
            m_regfile.set_busy(pidx);
            m_rat[inst->rd.atag] = pidx;
        }

        if (m_rob.full()) {
            m_r1_buf.push_front(inst);
            break;
        }
        std::string id_pre = inst->rid.idx == 0 ? "++" : "--";
        LOG_LEVEL1("RenameUnit R1", "r1 pc:0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx, rs2: v-%d-a%d-p%d-0x%lx,"
                   "rd: v%d-a%d-p%d-0x%lx%s%d",
                   inst->pc, inst->inst_name.c_str(), inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
                   inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val,
                   inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val, id_pre.c_str(), inst->rid.idx);
        auto alloc = m_rob.alloc(inst);
        if (!alloc) {
            m_r1_buf.push_front(inst);
            break;
        }
        if (is_branch(inst->opcode)) {
            RatCheckpoint cp;
            cp.valid = true;
            cp.rat = m_rat;
            cp.free_list = m_free_list;
            cp.c_id = m_checkpoint_id++;
            inst->c_id = cp.c_id;
            cp.rid = inst->rid;
            m_checkpoints.emplace_back(cp);
        }

        inst->rid = alloc.value();
        m_r2_buf.push_back(inst);
        ++n;
    }
}

void RenameUnit::r2() {
    size_t n = 0;
    while (n < m_rename_width && !m_r2_buf.empty()) {
        MInstPtr uop = m_r2_buf.front();
        m_r2_buf.pop_front();
        dispatch(uop);
        ++n;
    }
}

void RenameUnit::recycle_phys(i32 phys) {
    if (phys >= 0) {
        m_free_list.push_back(phys);
    }
}

void RenameUnit::build() {
    for (u32 i = 0; i < RV_GPR_SIZE; ++i) {
        m_rat[i] = i;
    }
    for (u32 i = 1; i < m_prf_capacity; ++i) {
        m_free_list.push_back(i);
    }
    return;
}

void RenameUnit::commit() {
    m_rename_issue_q.commit();
}

bool RenameUnit::dispatch(MInstPtr inst) {
    if (!inst->valid) {
        return false;
    }
    std::string id_pre = inst->rid.idx == 0 ? "++" : "--";
    LOG_LEVEL1("RenameUnit R2", "dispatch inst: %s, v%d-a%d-p%d%s%d",
               inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
               inst->rd.ptag, id_pre.c_str(), inst->rid.idx);
    m_rename_issue_q.write(inst);
    return true;
}


void RenameUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    m_r1_buf.clear();
    m_r2_buf.clear();
    m_rename_issue_q.clear();
    if (m_flush_event.scope == FlushScope::ALL) {
        m_rat = m_crat;
        m_checkpoints.clear();
        /* TODO:rebuild free list from committed state */
        assert(0);
        return;
    }
    if (!m_flush_event.recover_c_id) {
        return;
    }
    auto it = std::find_if(m_checkpoints.begin(), m_checkpoints.end(),
        [&](const RatCheckpoint& cp) {
            return cp.c_id == m_flush_event.c_id;
        });
    if (it == m_checkpoints.end())
        return;
    m_rat = it->rat;
    m_free_list = it->free_list;
    // 这个 checkpoint 以及比它年轻的 checkpoint 都失效。
    m_checkpoints.erase(it, m_checkpoints.end());
    return;
}
