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
{
    for (int i = 0; i < RV_GPR_SIZE; ++i) {
        m_rat[i] = i;
        m_crat[i] = i;
    }
    for (int p = RV_GPR_SIZE; p < RV_PHY_REG_SIZE; ++p)
        m_free_list.push_back(p);
}

static bool has_rs1(InstOp op) {
    switch (op) {
    case OP_LUI: case OP_AUIPC: case OP_JAL:
        return false;
    default:
        return true;
    }
}

static bool has_rs2(InstOp op) {
    switch (op) {
    case OP_LUI: case OP_AUIPC: case OP_JAL:
    case OP_JALR:
    case OP_ADDI: case OP_SLTI: case OP_SLTIU: case OP_XORI: case OP_ORI: case OP_ANDI:
    case OP_SLLI: case OP_SRLI: case OP_SRAI:
    case OP_ADDIW: case OP_SLLIW: case OP_SRLIW: case OP_SRAIW:
        return false;
    default:
        return true;
    }
}

static bool writes_rd(InstOp op) {
    switch (op) {
    case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE: case OP_BLTU: case OP_BGEU:
    case OP_SB: case OP_SH: case OP_SW: case OP_SD:
    case OP_FENCE: case OP_FENCE_I:
    case OP_ECALL: case OP_EBREAK:
        return false;
    default:
        return true;
    }
}

void RenameUnit::r0() {
    if (m_decode_rename_q.empty())
        return;
    size_t n = 0;
    MicroOp inst;
    while (n < m_rename_width && m_decode_rename_q.read(inst)) {
        if (!inst.valid)
            continue;
        m_r1_buf.push_back(std::move(inst));
        ++n;
    }
}

void RenameUnit::r1() {
    if (m_r1_buf.empty())
        return;
    size_t n = 0;
    while (n < m_rename_width && !m_r1_buf.empty()) {
        MicroOp inst = std::move(m_r1_buf.front());
        m_r1_buf.pop_front();

        int new_prd = -1;
        if (inst.rd.valid && inst.rd.atag < RV_GPR_SIZE) {
            if (m_free_list.empty()) {
                m_r1_buf.push_front(std::move(inst));
                break;
            }
            new_prd = m_free_list.front();
            m_free_list.pop_front();
            inst.rd.ptag = new_prd;
        }
        if (m_rob.full()) {
            m_r1_buf.push_front(std::move(inst));
            break;
        }
        inst.rob_id = m_rob.alloc(inst);
        m_r2_buf.push_back(std::move(inst));
        ++n;
    }
}

void RenameUnit::r2() {
    size_t n = 0;
    while (n < m_rename_width && !m_r2_buf.empty()) {
        MicroOp uop = std::move(m_r2_buf.front());
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

bool RenameUnit::dispatch(MicroOp& inst) {
    if (!inst.valid)
        return false;
    m_rename_issue_q.write(std::move(inst));
    return true;
}
