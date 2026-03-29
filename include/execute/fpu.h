#ifndef RV_TCORE_FPU_H
#define RV_TCORE_FPU_H

#include "alu.h"

class FPU : public DevIntf {
    constexpr static u32 MAX_STAGES = 8;
public:
    FPU(const FPUConfig& conf) : DevIntf("fpu") {
        m_stages = conf.stages;
        m_latency = conf.latency;
        for (i32 i = 0; i < m_stages; i++) {
            m_pipe[i].valid = false;
            m_pipe[i].inst = nullptr;
        }
    }
    ~FPU() = default;

    void tick() {};
    void commit() {}
    void build() {}

private:
    i32                                     m_stages { 0 };
    FUStage                                 m_pipe[MAX_STAGES];
    u32                                     m_latency { 1 };
};

#endif
