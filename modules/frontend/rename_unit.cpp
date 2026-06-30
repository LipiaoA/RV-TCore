#include "rename_unit.h"
#include "rob/rob.h"
#include "issue/issue_unit.h"
#include "isa/opcode.h"
#include "perf/perf_stats.h"
#include "regfile_unit.h"
#include "vregfile_unit.h"
#include "tregfile_unit.h"
#include "execute/lsu/lsu.h"
#include "isa/decode.h"

namespace {
bool is_lsq_type(InstType type)
{
    return type == InstType::LOAD || type == InstType::STA
        || type == InstType::STD || type == InstType::STORE;
}


}

RenameUnit::RenameUnit(const RenameConfig& conf, const RenameDeps& deps)
    : IRename("rename")
    , m_decode_rename_q(*deps.decode_rename_q)
    , m_rename_issue_q(*deps.rename_issue_q)
    , m_rob(*deps.rob)
    , m_regfile(*deps.regfile)
    , m_vregfile(*deps.vregfile)
    , m_tregfile(*deps.tregfile)
    , m_lsu(*deps.lsu)
    , m_crat(*deps.crat)
    , m_vcrat(*deps.vcrat)
    , m_tcrat(*deps.tcrat)
    , m_checkpoints(*deps.checkpoints)
    , m_flush_event(*deps.flush_event)
    , m_stats(deps.perf_stats)
    , m_rename_width(conf.width)
    , m_prf_capacity(conf.prf_capacity)
    , m_vprf_capacity(conf.vprf_capacity)
    , m_tprf_capacity(conf.tprf_capacity)
{
    init_rat(m_crat);
    init_vrat(m_vcrat);
    init_trat(m_tcrat);
    m_checkpoint_id = 1;
}

void RenameUnit::tick() {
    flush();
    r2();
    r1();
    r0();
}

void RenameUnit::r0() {
    if (m_decode_rename_q.empty()) {
        return;
    }
    size_t n = 0;
    MInstPtr inst;
    while (n < m_rename_width && m_decode_rename_q.read(inst)) {
        if (!inst->valid) {
            ++n;
            continue;
        }
        if (m_stats) {
            ++m_stats->rename.input_insts;
        }
        LOG_LEVEL1("RenameUnit R0", "r0 pc:0x%lx, inst: %s", inst->pc, inst->inst_name.c_str());
        m_r1_buf.push_back(inst);
        ++n;
    }
}

bool RenameUnit::needs_gpr_free_entry(const MInstPtr& inst) const
{
    return inst->rd.valid && inst->rd.rtyp == RegType::GPR;
}

bool RenameUnit::needs_vec_free_entry(const MInstPtr& inst) const
{
    return inst->type == InstType::VECTOR && inst->vd.valid;
}

bool RenameUnit::needs_tile_free_entry(const MInstPtr& inst) const
{
    return inst->type == InstType::TENSOR && inst->td.valid;
}

void RenameUnit::rename_src_gpr(Operand& src)
{
    if (!src.valid) {
        return;
    }
    src.ptag = m_rat[src.atag];
    src.ready = m_regfile.is_ready(src.ptag);
    return;
}

void RenameUnit::rename_src_vec(Operand& src)
{
    if (!src.valid) {
        return;
    }
    src.ptag = m_vrat[src.atag];
    src.ready = m_vregfile.is_ready(src.ptag);
}

void RenameUnit::rename_src_tile(Operand& src)
{
    if (!src.valid) {
        return;
    }
    src.ptag = m_trat[src.atag];
    src.ready = m_tregfile.is_ready(src.ptag);
}

void RenameUnit::rename_rd_gpr(MInstPtr inst)
{
    if (!inst->rd.valid) {
        return;
    }
    if (inst->rd.atag != 0) {
        const i32 ptag = m_free_list.front();
        m_free_list.pop_front();
        m_regfile.set_busy(ptag);
        inst->rd.ptag = ptag;
        inst->rd.o_ptag = m_rat[inst->rd.atag];
        inst->rd.rtyp = RegType::GPR;
        m_rat[inst->rd.atag] = ptag;
    } else {
        inst->rd.ptag = 0;
        m_rat[0] = 0;
    }
}

void RenameUnit::rename_rd_vec(MInstPtr inst)
{
    if (!inst->vd.valid) {
        return;
    }
    if (inst->vd.atag == 0) {
        inst->vd.ptag = 0;
        m_vrat[0] = 0;
        return;
    }
    if (inst->lmul > 1) {
        allocate_vec_group(inst->vd, inst->lmul, inst->vd.atag);
        return;
    }
    const i32 ptag = m_vfree_list.front();
    m_vfree_list.pop_front();
    m_vregfile.set_busy(ptag);
    inst->vd.ptag = ptag;
    inst->vd.o_ptag = m_vrat[inst->vd.atag];
    inst->vd.rgrp = 1;
    m_vrat[inst->vd.atag] = ptag;
}

void RenameUnit::rename_rd_tile(MInstPtr inst)
{
    if (!inst->td.valid) {
        return;
    }
    const i32 ptag = m_tfree_list.front();
    m_tfree_list.pop_front();
    m_tregfile.set_busy(ptag);
    inst->td.ptag = ptag;
    inst->td.o_ptag = m_trat[inst->td.atag];
    m_trat[inst->td.atag] = ptag;
}

bool RenameUnit::lsq_resources_available(const MInstPtr& inst) const
{
    if (!inst || inst->fetch_fault || !is_lsq_type(inst->type)) {
        return true;
    }
    return m_lsu.can_alloc(inst);
}

bool RenameUnit::rob_allocate(MInstPtr inst)
{
    auto alloc = m_rob.alloc(inst);
    if (!alloc) {
        return false;
    }
    inst->rid = alloc.value();
    return true;
}

bool RenameUnit::lsq_allocate(MInstPtr inst)
{
    if (!is_lsq_type(inst->type)) {
        return true;
    }
    if (!m_lsu.can_alloc(inst)) {
        return false;
    }
    return true;
}

bool RenameUnit::save_branch_checkpoint(MInstPtr inst)
{
    if (!isa::is_branch(inst->opcode)) {
        return false;
    }
    RatCheckpoint cp;
    cp.valid = true;
    cp.rat = m_rat;
    cp.vrat = m_vrat;
    cp.trat = m_trat;
    cp.free_list = m_free_list;
    cp.vfree_list = m_vfree_list;
    cp.tfree_list = m_tfree_list;
    cp.c_id = m_checkpoint_id++;
    inst->c_id = cp.c_id;
    cp.rid = inst->rid;
    m_checkpoints.emplace_back(cp);
    return true;
}

bool RenameUnit::allocate_vec_group(Operand& dst, u32 group, u32 base_atag)
{
    if (group == 0) {
        group = 1;
    }
    if (m_vfree_list.size() < group) {
        return false;
    }
    const i32 head_ptag = m_vfree_list.front();
    m_vfree_list.pop_front();
    m_vregfile.set_busy(head_ptag);
    dst.ptag = head_ptag;
    dst.o_ptag = m_vrat[base_atag];
    dst.rtyp = RegType::VEC;
    dst.rgrp = group;
    m_vrat[base_atag] = head_ptag;

    for (u32 i = 1; i < group; ++i) {
        if (base_atag + i >= RV_VREG_SIZE) {
            return false;
        }
        const i32 extra_ptag = m_vfree_list.front();
        m_vfree_list.pop_front();
        m_vregfile.set_busy(extra_ptag);
        m_vrat[base_atag + i] = extra_ptag;
    }
    return true;
}

void RenameUnit::r1() {
    if (m_r1_buf.empty()) {
        return;
    }
    size_t n = 0;
    while (n < m_rename_width && !m_r1_buf.empty()) {
        MInstPtr inst = m_r1_buf.front();
        m_r1_buf.pop_front();

        if (!inst->fetch_fault) {
            if (needs_gpr_free_entry(inst) && m_free_list.empty()) {
                if (m_stats) {
                    ++m_stats->rename.free_list_stall_cycles;
                }
                m_r1_buf.push_front(inst);
                break;
            }
            if (needs_vec_free_entry(inst)) {
                const u32 need = inst->lmul > 1 ? inst->lmul : 1;
                if (m_vfree_list.size() < need) {
                    m_r1_buf.push_front(inst);
                    break;
                }
            }
            if (needs_tile_free_entry(inst) && m_tfree_list.empty()) {
                m_r1_buf.push_front(inst);
                break;
            }

            if (inst->type == InstType::VECTOR) {
                rename_src_vec(inst->vs1);
                rename_src_vec(inst->vs2);
                if (inst->rs1.valid) {
                    rename_src_gpr(inst->rs1);
                }
                rename_rd_vec(inst);
            } else if (inst->type == InstType::TENSOR) {
                rename_src_tile(inst->ts1);
                rename_src_tile(inst->ts2);
                rename_rd_tile(inst);
            } else {
                rename_src_gpr(inst->rs1);
                rename_src_gpr(inst->rs2);
                rename_rd_gpr(inst);
                LOG_LEVEL1("RenameUnit R1", "r1 pc:0x%lx, inst: %s, rs1: v%d-a%d-p%d-0x%lx,"
                    " rs2: v%d-a%d-p%d-0x%lx, rd: v%d-a%d-p%d-0x%lx",
                    inst->pc, inst->inst_name.c_str(),
                    inst->rs1.valid, inst->rs1.atag, inst->rs1.ptag, inst->rs1.val,
                    inst->rs2.valid, inst->rs2.atag, inst->rs2.ptag, inst->rs2.val,
                    inst->rd.valid, inst->rd.atag, inst->rd.ptag, inst->rd.val);
            }
        }

        if (m_stats) {
            ++m_stats->rename.renamed_insts;
        }
        inst->state = InstState::RENAMED;
        m_r2_buf.push_back(inst);
        ++n;
    }
}

void RenameUnit::r2() {
    size_t n = 0;
    while (n < m_rename_width && !m_r2_buf.empty()) {
        MInstPtr inst = m_r2_buf.front();
        if (!inst || !inst->valid) {
            m_r2_buf.pop_front();
            continue;
        }

        const u32 rob_need = 1;
        if (!m_rob.has_space(rob_need)) {
            if (m_stats) {
                ++m_stats->rename.rob_full_stall_cycles;
            }
            break;
        }
        if (!lsq_resources_available(inst)) {
            break;
        }

        if (inst->fetch_fault) {
            if (!rob_allocate(inst)) {
                break;
            }
            inst->state = InstState::EXECUTED;
            m_rob.mark_ready(inst->rid, 0);
            m_r2_buf.pop_front();
            ++n;
            continue;
        }

        if (!can_dispatch(inst)) {
            break;
        }

        if (!lsq_allocate(inst)) {
            break;
        }

        if (!rob_allocate(inst)) {
            assert(false);
        }

        m_lsu.allocate(inst);

        if (inst->type == InstType::STORE) {
            MInstPtr inst2 = new MicroOp();
            *inst2 = *inst;
            inst2->type = InstType::STA;
            inst2->rs2.valid = false; // rs2 is the data to be stored

            if (!dispatch(inst2)) {
                delete inst2;
                assert(false);
            }
            inst->type = InstType::STD;
            inst->rs1 = inst->rs2;
            inst->rs2.valid = false;
        }


        if (!dispatch(inst)) {
            m_r2_buf.push_front(inst);
            assert(false);
        }
        save_branch_checkpoint(inst);
        m_r2_buf.pop_front();
        ++n;
    }
}

void RenameUnit::recycle_phys(i32 phys) {
    if (phys >= 0) {
        m_free_list.push_back(phys);
    }
}

void RenameUnit::recycle_vec_phys(i32 phys, u32 group) {
    if (phys < 0) {
        return;
    }
    m_vfree_list.push_back(phys);
    for (u32 i = 1; i < group; ++i) {
        // group members are tracked via arch rat on commit; only head recycled here
    }
}

void RenameUnit::recycle_tile_phys(i32 phys) {
    if (phys >= 0) {
        m_tfree_list.push_back(phys);
    }
}

void RenameUnit::build() {
    init_rat(m_crat);
    init_vrat(m_vcrat);
    init_trat(m_tcrat);
}

void RenameUnit::init_rat(const std::array<i32, RV_GPR_SIZE>& rat) {
    m_rat = rat;
    m_rat[0] = 0;
    rebuild_free_list();
}

void RenameUnit::init_vrat(const std::array<i32, RV_VREG_SIZE>& vrat) {
    m_vrat = vrat;
    m_vrat[0] = 0;
    rebuild_vfree_list();
}

void RenameUnit::init_trat(const std::array<i32, RV_TILE_SIZE>& trat) {
    m_trat = trat;
    rebuild_tfree_list();
}

void RenameUnit::rebuild_free_list() {
    std::vector<bool> used(m_prf_capacity, false);
    used[0] = true;
    for (i32 ptag : m_rat) {
        if (ptag >= 0 && static_cast<u32>(ptag) < m_prf_capacity) {
            used[static_cast<size_t>(ptag)] = true;
        }
    }

    m_free_list.clear();
    for (u32 p = 1; p < m_prf_capacity; ++p) {
        if (!used[p]) {
            m_free_list.push_back(static_cast<i32>(p));
        }
    }
}

void RenameUnit::rebuild_vfree_list() {
    std::vector<bool> used(m_vprf_capacity, false);
    used[0] = true;
    for (i32 ptag : m_vrat) {
        if (ptag >= 0 && static_cast<u32>(ptag) < m_vprf_capacity) {
            used[static_cast<size_t>(ptag)] = true;
        }
    }
    m_vfree_list.clear();
    for (u32 p = RV_VREG_SIZE; p < m_vprf_capacity; ++p) {
        if (!used[p]) {
            m_vfree_list.push_back(static_cast<i32>(p));
        }
    }
}

void RenameUnit::rebuild_tfree_list() {
    std::vector<bool> used(m_tprf_capacity, false);
    for (i32 ptag : m_trat) {
        if (ptag >= 0 && static_cast<u32>(ptag) < m_tprf_capacity) {
            used[static_cast<size_t>(ptag)] = true;
        }
    }
    m_tfree_list.clear();
    for (u32 p = RV_TILE_SIZE; p < m_tprf_capacity; ++p) {
        if (!used[p]) {
            m_tfree_list.push_back(static_cast<i32>(p));
        }
    }
}

void RenameUnit::commit() {
    m_rename_issue_q.commit();
}

bool RenameUnit::dispatch(MInstPtr inst)
{
    if (m_rename_issue_q.write(inst)) {
        if (m_stats) {
            ++m_stats->rename.dispatched_insts;
        }
        return true;
    }
    return false;
}

bool RenameUnit::can_dispatch(MInstPtr inst) const {
    if (!inst || !inst->valid) {
        return false;
    }
    size_t need = inst->type == InstType::STORE ? 2 : 1;
    return m_rename_issue_q.wsize() + need <= m_rename_issue_q.capacity();
}

void RenameUnit::flush() {
    if (!m_flush_event.valid) {
        return;
    }
    if (m_stats) {
        m_stats->rename.flushed_insts += m_r1_buf.size() + m_r2_buf.size();
        m_rename_issue_q.for_each([&](const MInstPtr& inst) {
            if (inst && inst->valid) {
                ++m_stats->rename.flushed_insts;
            }
        });
    }
    auto recycle_flushed = [&](const MInstPtr& inst) {
        if (!inst || !inst->valid) {
            return;
        }
        if (inst->rd.valid && inst->rd.rtyp == RegType::GPR) {
            m_free_list.push_back(inst->rd.ptag);
        }
        if (inst->vd.valid && inst->type == InstType::VECTOR) {
            m_vfree_list.push_back(inst->vd.ptag);
        }
        if (inst->td.valid && inst->type == InstType::TILE) {
            m_tfree_list.push_back(inst->td.ptag);
        }
        inst->valid = false;
    };

    if (m_flush_event.scope == FlushScope::ALL) {
        m_r1_buf.clear();
        m_r2_buf.clear();
        m_rename_issue_q.clear();
        m_checkpoints.clear();
        m_rat = m_crat;
        m_vrat = m_vcrat;
        m_trat = m_tcrat;
        rebuild_free_list();
        rebuild_vfree_list();
        rebuild_tfree_list();
        return;
    }
    if (!m_flush_event.recover_c_id) {
        assert(0);
        return;
    }
    m_r1_buf.clear();
    for (auto& inst : m_r2_buf) {
        recycle_flushed(inst);
    }
    m_r2_buf.clear();
    m_rename_issue_q.for_each([&](const MInstPtr& inst) {
        recycle_flushed(inst);
    });
    m_rename_issue_q.clear();
    auto it = std::find_if(m_checkpoints.begin(), m_checkpoints.end(),
        [&](const RatCheckpoint& cp) {
            return cp.c_id == m_flush_event.c_id;
        });
    if (it == m_checkpoints.end()) {
        assert(false);
        return;
    }
    m_rat = it->rat;
    m_vrat = it->vrat;
    m_trat = it->trat;
    m_free_list = it->free_list;
    m_vfree_list = it->vfree_list;
    m_tfree_list = it->tfree_list;
    m_checkpoints.erase(it, m_checkpoints.end());
}

void RenameUnit::reset() {
    m_r1_buf.clear();
    m_r2_buf.clear();
    m_rename_issue_q.clear();
    m_checkpoints.clear();
    m_checkpoint_id = 1;
    init_rat(m_crat);
    init_vrat(m_vcrat);
    init_trat(m_tcrat);
}
