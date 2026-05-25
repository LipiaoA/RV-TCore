
#include "branch_predictor.h"

BranchPredictor::BranchPredictor(const BPConfig& config)
    : m_time(0),
      m_ras(config.ras_capacity),
      m_ibtb(config.btb_config),
      m_tage(config.tage_config),
      m_bim(config.bim_config),
      m_fetch_width(config.bim_config.fetch_width)
{
}

void BranchPredictor::update() {
    return;
}

BranchPredictor::~BranchPredictor() {
}

void BranchPredictor::reset() {
    m_ibtb.invalidate_all();
    m_ras.reset();
    m_time = 0;
    return;
}

branch_pred BranchPredictor::f1_pred(pc_t pc) {
    branch_pred res;
    pc_t pred_pc = pc;
    for (u32 i = 0; i < m_fetch_width; i++) {
        pred_pc = pred_pc + i * 4;
        auto resp = m_ibtb.lookup(pred_pc, m_time);
        res.pred[i].valid = resp.hit;
        res.pred[i].target = -1ull;
        switch (resp.type) {
            case BrType::RET: {
                if (!m_ras.empty()) {
                    res.pred[i].taken = true;
                    res.pred[i].target = m_ras.top();
                }
            } break;
            case BrType::CALL: {
                m_ras.push_spec(pc + 4);
                res.pred[i].target = resp.target;
            } break;
            case BrType::JALR:
            case BrType::JAL: {
                res.pred[i].target = resp.target;
            } break;
            case BrType::COND: {
                res.pred[i].taken = resp.hit;
                res.pred[i].target = resp.target;
            } break;
            default: break;
        }
    }

    return res;
}

branch_pred BranchPredictor::f2_pred(pc_t pc) {
    /* branch_pred res;
    pc_t pred_pc = pc;
    for (u32 i = 0; i < m_fetch_width; i++) {
        pred_pc = pred_pc + i * 4;
        auto resp = m_bim.predict(pred_pc);
        res.pred[i].valid = resp.pred[i].valid;
        res.pred[i].taken = resp.pred[i]taken;
        res.pred[i].target = 0ull;
    } */
    return m_bim.predict(pc);
}

TagePred BranchPredictor::f3_pred(pc_t pc) {
    return m_tage.predict(pc);
}
