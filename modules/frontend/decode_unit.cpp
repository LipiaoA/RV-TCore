#include "decode_unit.h"
#include "decode/inst.h"
#include "log.h"
#include "isa/isa_inst.def"
#include "isa/decode.h"

DecodeUnit::DecodeUnit(const DecodeConfig& conf, const DecodeDeps& deps)
    : m_decode_width(conf.width)
    , m_fetch_decode_q(*deps.fetch_decode_q)
    , m_decode_rename_q(*deps.decode_rename_q)
    , m_flush_event(*deps.flush_event)
{
    m_decoded_q.clear();
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
        if (!isa::decode(finst.insn, dinst) || dinst->opcode == OP_INVALID)
            continue;
        LOG_LEVEL1("DecodeUnit d0", "decode pc:0x%lx inst: %s ", dinst->pc, dinst->inst_name.c_str());
        dinst->valid = true;
        dinst->fetch_id = finst.fetch_id;
        dinst->pred_taken = (pkt.br_mask & (1u << i)) ? pkt.pred_taken : false;
        dinst->type = isa::inst_type(dinst->opcode);
        dinst->ls_width = isa::ls_width(dinst->opcode);
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
            dinst->imm = isa::extract_imm(dinst->raw_inst, dinst->format, dinst->opcode);
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
