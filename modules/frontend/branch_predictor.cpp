#include "branch_predictor.h"

namespace {

TagePred tage_from(const BranchUpdate& info)
{
    TagePred pred;
    pred.valid = info.tage_valid;
    pred.final_pred = info.tage_final;
    pred.provider = info.tage_provider;
    pred.alt_provider = info.tage_alt_provider;
    pred.alt_pred = info.tage_alt_pred;
    return pred;
}

} // namespace

BranchPredictor::BranchPredictor(const BPConfig& config)
    : m_time(0),
      m_ras(config.ras_capacity),
      m_ibtb(config.btb_config),
      m_tage(config.tage_config),
      m_bim(config.bim_config),
      m_fetch_width(config.bim_config.fetch_width)
{
}

BranchPredictor::~BranchPredictor() {
}

void BranchPredictor::reset() {
    m_ibtb.invalidate_all();
    m_ras.reset();
    m_bim.rollback(0);
    m_tage.reset();
    m_time = 0;
    m_anchor_ghr = 0;
    m_anchor_ras = 0;
    m_anchor_valid = true;
}

branch_pred BranchPredictor::f1_pred(pc_t pc, u32 nslots) {
    branch_pred res;
    bool efficient = false;
    res.ras_sp = m_ras.checkpoint().sp;
    if (nslots > MAX_PREDICT_WIDTH) {
        nslots = MAX_PREDICT_WIDTH;
    }

    for (u32 i = 0; i < nslots; i++) {
        pc_t pred_pc = pc + static_cast<pc_t>(i) * 4;
        auto resp = m_ibtb.lookup(pred_pc, ++m_time);
        res.pred[i].type = resp.hit ? resp.type : BrType::NONE;
        if (!resp.hit) {
            continue;
        }

        res.pred[i].valid = true;
        switch (resp.type) {
            case BrType::RET: {
                if (!m_ras.empty()) {
                    res.pred[i].taken = true;
                    res.pred[i].target_valid = true;
                    res.pred[i].target = m_ras.top();
                    m_ras.pop_spec();
                } else {
                    res.pred[i].valid = false;
                }
            } break;
            case BrType::CALL: {
                m_ras.push_spec(pred_pc + 4);
                res.pred[i].taken = true;
                res.pred[i].target_valid = true;
                res.pred[i].target = resp.target;
            } break;
            case BrType::JALR:
            case BrType::JAL: {
                res.pred[i].taken = true;
                res.pred[i].target_valid = true;
                res.pred[i].target = resp.target;
            } break;
            case BrType::COND: {
                res.pred[i].taken = true;
                res.pred[i].target_valid = true;
                res.pred[i].target = resp.target;
            } break;
            default:
                res.pred[i].valid = false;
                break;
        }
        efficient = true;
        LOG_LEVEL1("BranchPredictor", "F1 Predict PC: 0x%lx Type: %s Taken: %d Target: 0x%lx",
                    pred_pc, BrTypeStr[static_cast<u32>(resp.type)], res.pred[i].taken, res.pred[i].target);
        break;
    }
    return res;
}

branch_pred BranchPredictor::f2_pred(pc_t pc, const branch_pred& f1, u32 nslots) {
    if (nslots > MAX_PREDICT_WIDTH) {
        nslots = MAX_PREDICT_WIDTH;
    }
    u32 cond_mask = 0;
    for (u32 i = 0; i < nslots; i++) {
        if (!f1.pred[i].valid) {
            continue;
        }
        if (f1.pred[i].type == BrType::COND) {
            cond_mask |= 1u << i;
        }
        break;
    }

    branch_pred dir = m_bim.predict(pc, nslots, cond_mask);
    branch_pred out = f1;
    out.ghr_snapshot = dir.ghr_snapshot;
    for (u32 i = 0; i < nslots; i++) {
        if (!dir.pred[i].valid) {
            continue;
        }
        out.pred[i].taken = dir.pred[i].taken;
        out.pred[i].valid = true;
        out.pred[i].type = BrType::COND;
    }
    return out;
}

TagePred BranchPredictor::f3_pred(pc_t pc) {
    return m_tage.predict(pc);
}

void BranchPredictor::resync(u64 ghr, u32 ras_sp, BrType type, bool taken, pc_t pc) {
    m_bim.rollback(ghr);
    if (type == BrType::COND) {
        m_bim.shift(taken);
    }
    RAS::Checkpoint cp;
    cp.sp = ras_sp;
    m_ras.restore(cp);
    if (taken && type == BrType::CALL) {
        m_ras.push_spec(pc + 4);
    } else if (taken && type == BrType::RET) {
        m_ras.pop_spec();
    }
}

void BranchPredictor::shift_cond(bool taken) {
    m_bim.shift(taken);
}

void BranchPredictor::rollback(u64 ghr, u32 ras_sp, BrType type, bool taken, pc_t pc) {
    resync(ghr, ras_sp, type, taken, pc);
    m_anchor_ghr = m_bim.ghr();
    m_anchor_ras = m_ras.checkpoint().sp;
    m_anchor_valid = true;
}

void BranchPredictor::squash_to_anchor() {
    m_bim.rollback(m_anchor_ghr);
    RAS::Checkpoint cp;
    cp.sp = m_anchor_ras;
    m_ras.restore(cp);
}

void BranchPredictor::note_anchor(const BranchUpdate& info) {
    if (!m_anchor_valid) {
        m_anchor_ghr = 0;
        m_anchor_ras = 0;
        m_anchor_valid = true;
    }
    if (info.ghr_snapshot != m_anchor_ghr || info.ras_sp != m_anchor_ras) {
        return;
    }
    if (info.type == BrType::COND) {
        m_anchor_ghr = m_bim.advance(m_anchor_ghr, info.taken);
    }
    if (info.taken && info.type == BrType::CALL) {
        m_anchor_ras = m_ras.next_sp(m_anchor_ras);
    } else if (info.taken && info.type == BrType::RET && m_anchor_ras > 0) {
        m_anchor_ras--;
    }
}

void BranchPredictor::update(const BranchUpdate& info) {
    LOG_LEVEL1("BranchPredictor", "Update PC: 0x%lx Type: %s Taken: %d Target: 0x%lx",
                info.pc, BrTypeStr[static_cast<u32>(info.type)], info.taken, info.target);
    if (info.type == BrType::NONE) {
        return;
    }
    if (info.type == BrType::COND) {
        m_bim.update(info.pc, info.taken, info.ghr_snapshot);
        TagePred pred = tage_from(info);
        m_tage.update(info.pc, info.taken, pred);
    }
    if (info.taken && info.target_valid) {
        m_ibtb.update(info.pc, info.target, info.type, static_cast<u32>(++m_time));
    }
    note_anchor(info);
}
