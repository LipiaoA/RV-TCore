#ifndef RV_TCORE_BRU_H
#define RV_TCORE_BRU_H

#include "module_comm.h"
#include "tools/intf.h"
#include "config.h"

class BRU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    BRU(const BRUConfig& conf);
    ~BRU() override = default;

    void                                    tick() override;
    void                                    commit() override;
    void                                    build() override {}

    bool                                    dispatch(MInstPtr inst);
    bool                                    accept() const;

protected:
    virtual void                            R0();
    virtual void                            R1();
    virtual void                            E0();
    virtual void                            flush();

    void                                    shift();

    void                                    send_redirect(pc_t target);
    void                                    send_flush(MInstPtr inst);

private:
    i32                                     m_stages { 4 };
    FUStage                                 m_pipe[MAX_STAGES];
    bool                                    m_pred_taken { false };
    pc_t                                    m_pred_target { 0 };
    void                                    (*exec)(MInstPtr inst) = nullptr;

    OUTPUT std::vector<RegReq>              m_reg_read_q;
    OUTPUT std::vector<FlushEvent>          m_flush_q;
    INPUT  std::vector<RegReq>              m_reg_resp;
};

#endif
