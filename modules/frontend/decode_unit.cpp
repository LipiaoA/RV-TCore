#include "decode_unit.h"
#include "decode/inst.h"
#include "log.h"
#include "isa/isa_inst.def"

i64 DecodeUnit::extract_imm(u64 raw, InstFmt fmt, InstOp op) {

    switch (fmt) {

    case RV_FMT_I:
        // 普通 I 型：直接使用 imm[11:0]；移位类指令单独根据 opcode 处理
        if (op == OP_SLLI || op == OP_SRLI || op == OP_SRAI) {
            // RV64: shamt 为 6bit，位于 [25:20]
            return (raw >> 20) & 0x3f;
        }
        if (op == OP_ADDIW || op == OP_SLLIW || op == OP_SRLIW || op == OP_SRAIW) {
            // RV64W: shamt 为 5bit
            if (op == OP_SLLIW || op == OP_SRLIW || op == OP_SRAIW)
                return (raw >> 20) & 0x1f;
            // addiw 仍按普通 I 型立即数处理
        }
        return sign_extend(raw >> 20, 12);

    case RV_FMT_S: {
        u64 imm =
            ((raw >> 7) & 0x1f) |
            ((raw >> 25) << 5);
        return sign_extend(imm, 12);
    }

    case RV_FMT_B: {
        u64 imm =
            ((raw >> 8) & 0x0f) << 1 |
            ((raw >> 25) & 0x3f) << 5 |
            ((raw >> 7) & 0x1) << 11 |
            ((raw >> 31) << 12);
        return sign_extend(imm, 13);
    }

    case RV_FMT_U:
        return (i64)(raw & 0xfffff000);

    case RV_FMT_J: {
        u64 imm =
            ((raw >> 21) & 0x3ff) << 1 |
            ((raw >> 20) & 0x1) << 11 |
            ((raw >> 12) & 0xff) << 12 |
            ((raw >> 31) << 20);
        return sign_extend(imm, 21);
    }

    default:
        return 0;
    }
}

bool DecodeUnit::decode(u64 raw, u64 pc, SInst *out) {
    static const u32 rv64i_inst_size = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
    for (size_t i = 0; i < rv64i_inst_size; i++) {
        if ((raw & rv64i_table[i].mask) == rv64i_table[i].match) {
            const InstOp op   = rv64i_table[i].opcode;
            const InstFmt fmt = rv64i_table[i].format;

            out->raw_inst = raw;
            out->pc       = pc;
            out->type     = op;

            out->rd  = (raw >> 7)  & 0x1f;
            out->rs1 = (raw >> 15) & 0x1f;
            out->rs2 = (raw >> 20) & 0x1f;

            // 对于 CSR / FENCE 等 RV_FMT_NONE 类型，这里 imm 保持为 0，
            // 具体语义由执行阶段使用 raw_inst 自行解析。
            if (fmt != RV_FMT_NONE)
                out->imm = (int32_t)extract_imm(raw, fmt, op);
            else
                out->imm = 0;

            return true;
        }
    }

    out->type = OP_INVALID;
    return false;
}

DecodeUnit::DecodeUnit(const std::string& n)
    : IDecode(n)
{
    m_decoded_q.clear();
    rv64i_insts_size = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
}

void DecodeUnit::set_fetch_decode_q(ASyncQueue<FetchPacket>* q) {
    m_fetch_decode_q = q;
}

void DecodeUnit::set_decode_rename_q(ASyncQueue<DecodedInst>* q) {
    m_decode_rename_q = q;
}

void DecodeUnit::attach_fetch(FetchUnit* f)
{
    m_fetch = f;
}

bool DecodeUnit::has_decoded() const
{
    return !m_decoded_q.empty();
}

bool DecodeUnit::try_pop_decoded(SInst& out)
{
    if (m_decoded_q.empty())
        return false;
    out = m_decoded_q.front();
    m_decoded_q.pop_front();
    return true;
}

void DecodeUnit::tick()
{
    if (!m_fetch)
        return;

    size_t decoded_this_cycle = 0;

    // 从 FetchUnit 拉取一个 fetch_packet（若有）
    auto pkt_opt = m_fetch->try_dispatch();
    if (!pkt_opt.has_value())
        return;

    const FetchPacket& pkt = pkt_opt.value();
    for (const auto& finst : pkt.insts) {
        if (!finst.valid)
            continue;
        if (decoded_this_cycle >= m_decode_width)
            break;

        SInst sinst{};
        if (decode(static_cast<u32>(finst.insn), finst.pc, &sinst)) {
            if (sinst.type != OP_INVALID) {
                m_decoded_q.push_back(sinst);
                ++decoded_this_cycle;
            }
        }
    }
}

void DecodeUnit::commit()
{
    // 当前实现中，DecodeUnit 没有跨周期的“next/current”状态需要在此阶段生效，
    // 保留为空实现以符合接口约定。
}
