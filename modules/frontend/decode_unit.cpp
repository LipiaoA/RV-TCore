#include "decode_unit.h"
#include "decode/inst.h"
#include "log.h"
#include "isa/isa_inst.def"

void DecodeUnit::predecode(insn_t raw, PredecodeInfo* out)
{
    out->valid = false;
    out->is_branch = out->is_load = out->is_store = out->is_fp = false;
    out->length = 4;
    out->slot = 0;
    static const u32 rv64i_sz = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
    for (u32 i = 0; i < rv64i_sz; ++i) {
        if ((raw & rv64i_table[i].mask) != rv64i_table[i].match)
            continue;
        InstOp op = rv64i_table[i].opcode;
        out->valid = true;
        if (is_branch(op))
            out->is_branch = true;
        else if (op >= OP_LB && op <= OP_LWU)
            out->is_load = true;
        else if (op >= OP_SB && op <= OP_SD)
            out->is_store = true;
        out->is_fp = false;  // RV64I 无浮点，预留
        break;
    }
}

i64 DecodeUnit::extract_imm(u64 raw, InstFmt fmt, InstOp op)
{

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
        return (i64)(sign_extend(raw & 0xfffff000, 32));

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

u32 DecodeUnit::get_ls_width(InstOp op) {
    switch (op) {
    case OP_LB:
    case OP_LBU:
    case OP_SB:
        return 1;
    case OP_SH:
    case OP_LH:
    case OP_LHU:
        return 2;
    case OP_SW:
    case OP_LW:
    case OP_LWU:
        return 4;
    case OP_SD:
    case OP_LD:
        return 8;
    default: break;
    }
    return 0;
}

namespace {
void map_vector_operands(MicroOp* out)
{
    out->vd.valid = out->rd.valid;
    out->vd.atag = out->rd.atag;
    out->vd.rtyp = RegType::VEC;
    out->rd.valid = false;

    const u32 funct3 = static_cast<u32>((out->raw_inst >> 12) & 0x7);
    if (funct3 == 4) {
        out->vs2.valid = out->rs2.valid;
        out->vs2.atag = out->rs2.atag;
        out->vs2.rtyp = RegType::VEC;
        out->rs2.valid = false;
        out->rs1.rtyp = RegType::GPR;
        return;
    }
    if (funct3 == 3) {
        out->vs2.valid = out->rs2.valid;
        out->vs2.atag = out->rs2.atag;
        out->vs2.rtyp = RegType::VEC;
        out->rs2.valid = false;
        return;
    }
    out->vs1.valid = out->rs1.valid;
    out->vs1.atag = out->rs1.atag;
    out->vs1.rtyp = RegType::VEC;
    out->vs2.valid = out->rs2.valid;
    out->vs2.atag = out->rs2.atag;
    out->vs2.rtyp = RegType::VEC;
    out->rs1.valid = false;
    out->rs2.valid = false;
}
}

bool DecodeUnit::decode(u64 raw, MicroOp *out) {
    static const u32 rv64i_inst_size = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
    for (size_t i = 0; i < rv64i_inst_size; i++) {
        if ((raw & rv64i_table[i].mask) == rv64i_table[i].match) {
            const InstOp op   = rv64i_table[i].opcode;
            const InstFmt fmt = rv64i_table[i].format;
            out->inst_name = rv64i_inst_map.at(op);;
            out->raw_inst = raw;
            out->opcode   = op;
            out->format   = fmt;
            out->type     = get_inst_type(op);
            out->ls_width = get_ls_width(op);
            out->rd.atag  = static_cast<i32>((raw >> 7) & 0x1f);
            out->rs1.atag = static_cast<i32>((raw >> 15) & 0x1f);
            out->rs2.atag = static_cast<i32>((raw >> 20) & 0x1f);
            switch (fmt) {
            case RV_FMT_R:
                out->rd.valid = true;
                out->rs1.valid = true;
                out->rs2.valid = true;
                break;
            case RV_FMT_I:
                out->rd.valid = true;
                out->rs1.valid = true;
                out->rs2.valid = false;
                break;
            case RV_FMT_S:
                out->rd.valid = false;
                out->rs1.valid = true;
                out->rs2.valid = true;
                break;
            case RV_FMT_B:
                out->rd.valid = false;
                out->rs1.valid = true;
                out->rs2.valid = true;
                break;
            case RV_FMT_U:
                out->rd.valid = true;
                out->rs1.valid = false;
                out->rs2.valid = false;
                break;
            case RV_FMT_J:
                out->rd.valid = true;
                out->rs1.valid = false;
                out->rs2.valid = false;
                break;
            case RV_FMT_A:
                out->rd.valid = true;
                out->rs1.valid = true;
                out->rs2.valid = true;
                break;
            case RV_FMT_C:
                /* compressed instr is not supported */
                out->rd.valid = false;
                out->rs1.valid = false;
                out->rs2.valid = false;
                out->opcode = OP_INVALID;
                return false;
            case RV_FMT_K:
            case RV_FMT_H:
            case RV_FMT_NONE:
                out->rd.valid = false;
                out->rs1.valid = false;
                out->rs2.valid = false;
                out->opcode = OP_INVALID;
                return false;
            }
            if (is_vector(op)) {
                map_vector_operands(out);
            }
            return true;
        }
    }
    out->opcode = OP_INVALID;
    return false;
}

DecodeUnit::DecodeUnit(const DecodeConfig& conf, const DecodeDeps& deps)
    : m_decode_width(conf.width)
    , m_fetch_decode_q(*deps.fetch_decode_q)
    , m_decode_rename_q(*deps.decode_rename_q)
    , m_flush_event(*deps.flush_event)
{
    m_decoded_q.clear();
    rv64i_insts_size = sizeof(rv64i_table) / sizeof(rv64i_table[0]);
}

bool DecodeUnit::has_decoded() const
{
    return !m_decoded_q.empty();
}

bool DecodeUnit::try_pop_decoded(MicroOp& out)
{
    if (m_decoded_q.empty())
        return false;
    out = m_decoded_q.front();
    m_decoded_q.pop_front();
    return true;
}

void DecodeUnit::tick()
{
    flush();
    if (m_stall) {
        return;
    }
    d1();
    d0();
}

void DecodeUnit::d0() {
    FetchPacket pkt;
    if (!m_fetch_decode_q.read(pkt) || !m_d0_pkt.empty())
        return;

    if (pkt.fetch_fault) {
        MInstPtr dinst = new MicroOp();
        dinst->valid = true;
        dinst->pc = pkt.fault_pc;
        dinst->fetch_id = pkt.fault_fetch_id;
        dinst->fetch_fault = true;
        dinst->has_exception = true;
        dinst->exception_cause = pkt.exception_cause;
        dinst->exception_tval = pkt.exception_tval;
        dinst->inst_name = "fetch_fault";
        dinst->type = InstType::NONE;
        LOG_LEVEL1("DecodeUnit d0", "fetch fault pc:0x%lx fetch_id:%lu cause:%lu",
                   dinst->pc, dinst->fetch_id, dinst->exception_cause);
        m_d0_pkt.emplace_back(dinst);
        return;
    }

    size_t inst_count = 0;
    for (size_t i = 0; i < pkt.insts.size() && inst_count < m_decode_width; ++i) {
        const auto& finst = pkt.insts[i];
        if (!finst.valid || !(pkt.valid_mask & (1u << i)))
            continue;

        MInstPtr dinst = new MicroOp();
        dinst->pc = finst.pc;
        if (!decode(finst.insn, dinst) || dinst->opcode == OP_INVALID)
            continue;
        LOG_LEVEL1("DecodeUnit d0", "decode pc:0x%lx inst: %s ", dinst->pc, dinst->inst_name.c_str());
        dinst->valid = true;
        dinst->fetch_id = finst.fetch_id;
        dinst->pred_taken = (pkt.br_mask & (1u << i)) ? pkt.pred_taken : false;
        if (is_store(dinst->opcode)) {
            dinst->type = InstType::STORE;
        }
        m_d0_pkt.emplace_back(dinst);
        ++inst_count;
    }
}

void DecodeUnit::d1() {
    std::vector<MInstPtr> retained;
    for (size_t i = 0; i < m_d0_pkt.size(); ++i) {
        auto& dinst = m_d0_pkt[i];
        if (!dinst->valid) {
            delete dinst;
            dinst = nullptr;
            continue;
        }
        // 对于 CSR / FENCE 等 RV_FMT_NONE 类型，这里 imm 保持为 0，
        // 具体语义由执行阶段使用 raw_inst 自行解析。
        if (dinst->format != RV_FMT_NONE)
            dinst->imm = extract_imm(dinst->raw_inst, dinst->format, dinst->opcode);
        else
            dinst->imm = 0;
        if (!m_decode_rename_q.write(dinst)) {
            for (size_t j = i; j < m_d0_pkt.size(); ++j)
                retained.emplace_back(m_d0_pkt[j]);
            break;
        }
        LOG_LEVEL1("DecodeUnit d1", "pc:0x%lx inst: %s ", dinst->pc, dinst->inst_name.c_str());
    }
    m_d0_pkt = std::move(retained);
}

void DecodeUnit::commit()
{
    m_decode_rename_q.commit();
}


InstType DecodeUnit::get_inst_type(InstOp op) {
    InstType type = InstType::NONE;
    if (is_branch(op)) {
        type = InstType::BRANCH;
    } else if (is_load(op)) {
        type = InstType::LOAD;
    } else if (is_store(op)) {
        type = InstType::STORE;
    } else if (is_fp(op)) {
        type = InstType::FP;
    } else if (is_vector(op)) {
        type = InstType::VECTOR;
    } else if (is_int(op)) {
        type = InstType::INT;
    }
    return type;
}

void DecodeUnit::flush()
{
    if (!m_flush_event.valid) {
        return;
    }
    switch (m_flush_event.scope) {
    case FlushScope::YOUNGER_THAN: {
        for (auto& dinst : m_d0_pkt) {
            if (dinst->fetch_id > m_flush_event.fetch_id) {
                dinst->valid = false;
            }
        }
        m_decode_rename_q.for_each_mut([&](MInstPtr& dinst) {
            if (dinst && dinst->valid && dinst->fetch_id > m_flush_event.fetch_id) {
                dinst->valid = false;
            }
        });
    } break;
    case FlushScope::ALL: {
        m_fetch_decode_q.clear();
        m_d0_pkt.clear();
        m_decode_rename_q.clear();
    } break;
    default:
        assert(false);
        break;
    }
    return;
}

void DecodeUnit::reset() {
    return;
}
