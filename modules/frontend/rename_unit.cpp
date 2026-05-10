#include "rename_unit.h"
#include "rob/rob.h"
#include "issue/issue_unit.h"
#include "isa/opcode.h"

RenameUnit::RenameUnit(const RenameConfig& conf, const RenameDeps& deps)
    : IRename("rename")
    , m_decode_rename_q(*deps.decode_rename_q)
    , m_rename_issue_q(*deps.rename_issue_q)
    , m_rob(*deps.rob)
    , m_rename_width(conf.width)
    , m_prf(conf.phys_n)
{
    // 0 is x0, is zero register
    for (int p = 1; p < conf.phys_n; ++p) {
        m_free_list.push_back(p);
    }
    m_rat[0] = 0;
}

void RenameUnit::r0() {
    if (m_decode_rename_q.empty())
        return;
    size_t n = 0;
    MInstPtr inst;
    while (n < m_rename_width && m_decode_rename_q.read(inst)) {
        if (!inst->valid)
            continue;
        m_r1_buf.push_back(inst);
        ++n;
    }
}

void RenameUnit::r1() {
    if (m_r1_buf.empty())
        return;
    size_t n = 0;
    while (n < m_rename_width && !m_r1_buf.empty()) {
        MInstPtr inst = m_r1_buf.front();
        m_r1_buf.pop_front();

        int pidx = -1;
        if (inst->rs1.valid) {
            inst->rs1.ptag = m_rat[inst->rs1.atag];
            inst->rs1.ready = m_prf.is_ready(inst->rs1.ptag);
        }
        if (inst->rs2.valid) {
            inst->rs2.ptag = m_rat[inst->rs2.atag];
            inst->rs2.ready = m_prf.is_ready(inst->rs2.ptag);
        }
        if (inst->rd.valid) {
            if (m_free_list.empty()) {
                m_r1_buf.push_front(inst);
                break;
            }
            pidx = m_free_list.front();
            m_free_list.pop_front();
            inst->rd.ptag = pidx;
            m_prf.set_busy(pidx);
            m_rat[inst->rd.atag] = pidx;
        }

        if (m_rob.full()) {
            m_r1_buf.push_front(inst);
            break;
        }
        std::string id_pre = inst->rob_id.idx == 0 ? "++" : "--";
        LOG_LEVEL1("RenameUnit R1", "r1 inst: %s, v%d-a%d-p%d%s%d",
                   inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
                   inst->rd.ptag, id_pre.c_str(), inst->rob_id.idx);
        inst->rob_id = m_rob.alloc(inst);
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

void RenameUnit::tick() {
    r2();
    r1();
    r0();
}

void RenameUnit::recycle_phys(int phys) {
    if (phys >= 0) {
        m_free_list.push_back(phys);
    }
}

void RenameUnit::build() {
    for (int i = 0; i < RV_GPR_SIZE; ++i) {
        m_rat[i] = i;
        m_crat[i] = i;
    }
    for (int i = RV_GPR_SIZE; i < RV_PHY_REG_SIZE; ++i) {
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
    std::string id_pre = inst->rob_id.idx == 0 ? "++" : "--";
    LOG_LEVEL1("RenameUnit R2", "dispatch inst: %s, v%d-a%d-p%d%s%d",
               inst->inst_name.c_str(), inst->rd.valid, inst->rd.atag,
               inst->rd.ptag, id_pre.c_str(), inst->rob_id.idx);
    m_rename_issue_q.write(inst);
    return true;
}
