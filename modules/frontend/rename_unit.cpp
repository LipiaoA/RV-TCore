#include "rename_unit.h"

void RenameUnit::set_decode_rename_q(ASyncQueue<DecodedInst>* q) {
    m_decode_rename_q = q;
}

void RenameUnit::set_rename_dispatch_q(ASyncQueue<DynInst>* q) {
    m_rename_dispatch_q = q;
}

RenameUnit::RenameUnit(const std::string& n)
    : IRename(n)
{
    // 初始化 RAT：逻辑寄存器 i 映射到物理寄存器 i
    m_rat.resize(kNumLogicalRegs);
    for (int i = 0; i < kNumLogicalRegs; ++i) {
        m_rat[i] = i;
    }

    // 初始化 FreeList：其余物理寄存器可用于重命名
    for (int p = kNumLogicalRegs; p < kNumPhysicalRegs; ++p) {
        m_free_list.push_back(p);
    }
}

void RenameUnit::push_from_decode(const SInst& s)
{
    m_decoded_q.push_back(s);
}

bool RenameUnit::has_renamed() const
{
    return !m_renamed_q.empty();
}

bool RenameUnit::try_pop_renamed(DynInst& out)
{
    if (m_renamed_q.empty())
        return false;
    out = m_renamed_q.front();
    m_renamed_q.pop_front();
    return true;
}

void RenameUnit::tick()
{
    size_t renamed_this_cycle = 0;

    while (!m_decoded_q.empty() && renamed_this_cycle < m_rename_width) {
        SInst s = m_decoded_q.front();
        m_decoded_q.pop_front();

        DynInst d{};
        d.sinst = nullptr;  // 简化：不直接持有指针，由上层按需管理
        d.rob_id = -1;

        // 源寄存器映射
        auto has_rs1 = true;
        auto has_rs2 = true;

        // 简单依据 opcode 约定源寄存器使用情况（后续可扩展为查 SUop 表）
        switch (s.type) {
        case OP_LUI:
        case OP_AUIPC:
        case OP_JAL:
            has_rs1 = false;
            has_rs2 = false;
            break;
        case OP_JALR:
            has_rs2 = false;
            break;
        case OP_ADDI:
        case OP_SLTI:
        case OP_SLTIU:
        case OP_XORI:
        case OP_ORI:
        case OP_ANDI:
        case OP_SLLI:
        case OP_SRLI:
        case OP_SRAI:
        case OP_ADDIW:
        case OP_SLLIW:
        case OP_SRLIW:
        case OP_SRAIW:
            has_rs2 = false;
            break;
        default:
            break;
        }

        if (has_rs1 && s.rs1 < kNumLogicalRegs)
            d.phys_rs1 = m_rat[s.rs1];
        else
            d.phys_rs1 = -1;

        if (has_rs2 && s.rs2 < kNumLogicalRegs)
            d.phys_rs2 = m_rat[s.rs2];
        else
            d.phys_rs2 = -1;

        d.rs1_ready = false;
        d.rs2_ready = false;

        // 目的寄存器重命名
        bool writes_rd = true;
        switch (s.type) {
        case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE:
        case OP_BLTU: case OP_BGEU:
        case OP_SB: case OP_SH: case OP_SW: case OP_SD:
        case OP_FENCE: case OP_FENCE_I:
        case OP_ECALL: case OP_EBREAK:
            writes_rd = false;
            break;
        default:
            break;
        }

        if (writes_rd && s.rd != 0 && s.rd < kNumLogicalRegs) {
            if (m_free_list.empty()) {
                // 无可用物理寄存器，前端暂时阻塞：将本条重新放回队列，并停止本周期重命名
                m_decoded_q.push_front(s);
                break;
            }
            int new_p = m_free_list.front();
            m_free_list.pop_front();

            m_rat[s.rd] = new_p;
            d.phys_rd = new_p;
        } else {
            d.phys_rd = -1;
        }

        m_renamed_q.push_back(d);
        ++renamed_this_cycle;
    }
}

void RenameUnit::commit()
{
    // 未来可在此根据 ROB 提交信息回收旧映射并归还物理寄存器
}
