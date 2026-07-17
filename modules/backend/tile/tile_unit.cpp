#include "execute/tile/tile_unit.h"

#include <algorithm>

TileUnit::TileUnit(const TileConfig& conf, const TileDeps& deps)
    : DevIntf("tile")
    , m_stages(conf.stages)
    , m_rob(*deps.rob)
    , m_wakeup_bus(*deps.wakeup_bus)
    , m_tpu_resp_q(*deps.tpu_resp_q)
{
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
    m_input_latch.valid = false;
    m_commit_latch.valid = false;
}

bool TileUnit::accept() const
{
    return !m_pipe[0].valid && !m_input_latch.valid;
}

bool TileUnit::dispatch(const MInstPtr inst)
{
    if (!accept() || !inst) {
        return false;
    }
    m_input_latch.inst = inst;
    m_input_latch.valid = true;
    return true;
}

void TileUnit::tick()
{
    E0();
    R1();
    shift();
}

void TileUnit::commit()
{
    if (m_input_latch.valid && !m_pipe[0].valid) {
        m_pipe[0] = m_input_latch;
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
}

void TileUnit::R1()
{
    if (!m_pipe[0].valid || !m_pipe[0].inst) {
        return;
    }
    MInstPtr inst = m_pipe[0].inst;
    auto read_tile = [&](Operand& src, TileReg& out) -> bool {
        for (const auto& resp : m_tpu_resp_q) {
            if (resp.valid && resp.ptag == src.ptag) {
                out = resp.val;
                return true;
            }
        }
        return false;
    };

    TileReg ts1_reg;
    TileReg ts2_reg;
    if (inst->ts1.valid && !read_tile(inst->ts1, ts1_reg)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
        return;
    }
    if (inst->ts2.valid && !read_tile(inst->ts2, ts2_reg)) {
        inst->fu_pending = false;
        inst->state = InstState::REPLAY;
        m_pipe[0].valid = false;
        m_pipe[0].inst = nullptr;
        return;
    }
    inst->fu_pending = false;
    inst->state = InstState::FU_ACTIVE;
    if (inst->td.valid) {
        wakeup(inst);
    }
}

void TileUnit::exec_tile(MInstPtr inst)
{
    TileReg result;
    result.reset(TILE_ROWS, TILE_COLS, TILE_ELEM_BITS);

    auto read_tile = [&](const Operand& src) -> TileReg {
        for (const auto& resp : m_tpu_resp_q) {
            if (resp.valid && resp.ptag == src.ptag) {
                return resp.val;
            }
        }
        return TileReg {};
    };

    if (inst->ts1.valid && inst->ts2.valid) {
        const TileReg lhs = read_tile(inst->ts1);
        const TileReg rhs = read_tile(inst->ts2);
        const u32 nbytes = std::min({result.byte_size(), lhs.byte_size(), rhs.byte_size()});
        for (u32 i = 0; i < nbytes; ++i) {
            result.data[i] = static_cast<u8>(lhs.data[i] + rhs.data[i]);
        }
        result.valid = true;
    } else if (inst->ts1.valid) {
        result = read_tile(inst->ts1);
        result.valid = true;
    }

    inst->td_payload = result;
}

void TileUnit::E0()
{
    FUStage& stage = m_pipe[m_stages - 1];
    if (!stage.valid || !stage.inst || m_commit_latch.valid) {
        return;
    }
    MInstPtr inst = stage.inst;
    exec_tile(inst);
    stage.valid = false;
    stage.inst = nullptr;
    m_commit_latch.inst = inst;
    m_commit_latch.valid = true;
}

void TileUnit::shift()
{
    for (i32 i = m_stages - 1; i > 0; --i) {
        if (!m_pipe[i].valid) {
            m_pipe[i] = m_pipe[i - 1];
            m_pipe[i - 1].valid = false;
            m_pipe[i - 1].inst = nullptr;
        }
    }
}

void TileUnit::wakeup(MInstPtr inst)
{
    if (!inst || !inst->td.valid) {
        return;
    }
    WakeupEvent event;
    event.valid = true;
    event.type = WAKEUP_BYPASS;
    event.ptag = inst->td.ptag;
    event.rid = inst->rid;
    event.rtyp = RegType::TILE;
    m_wakeup_bus.push_back(event);
}

bool TileUnit::commit_ready() const
{
    return m_commit_latch.valid;
}

MInstPtr TileUnit::commit_inst() const
{
    return m_commit_latch.inst;
}

void TileUnit::commit_ack()
{
    m_commit_latch.valid = false;
    m_commit_latch.inst = nullptr;
}

void TileUnit::flush(const FlushEvent& event)
{
    auto should_flush = [&](const MInstPtr& inst) -> bool {
        if (!inst) {
            return true;
        }
        if (event.scope == FlushScope::ALL) {
            return true;
        }
        return m_rob.is_older_than(event.rid, inst->rid);
    };

    if (m_input_latch.valid && should_flush(m_input_latch.inst)) {
        m_input_latch.valid = false;
        m_input_latch.inst = nullptr;
    }
    if (m_commit_latch.valid && should_flush(m_commit_latch.inst)) {
        m_commit_latch.valid = false;
        m_commit_latch.inst = nullptr;
    }
    for (i32 i = 0; i < m_stages; ++i) {
        if (m_pipe[i].valid && should_flush(m_pipe[i].inst)) {
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
}

void TileUnit::reset()
{
    m_input_latch.valid = false;
    m_commit_latch.valid = false;
    for (i32 i = 0; i < m_stages; ++i) {
        m_pipe[i].valid = false;
        m_pipe[i].inst = nullptr;
    }
}
