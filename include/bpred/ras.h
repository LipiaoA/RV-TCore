#ifndef RV_TCORE_RAS_H
#define RV_TCORE_RAS_H

#include "util.h"

class RAS {
public:
    RAS(u32 depth = 32);

    bool                                empty() const;
    pc_t                                top()   const;

    void                                push_spec(pc_t pc);
    void                                pop_spec();
    void                                reset();

    struct Checkpoint {
        size_t sp;
    };

    Checkpoint                          checkpoint() const;
    void                                restore(const Checkpoint& cp);

private:
    const u32                           DEPTH;
    std::vector<pc_t>                   m_stack = std::vector<pc_t>(DEPTH);
    u32                                 m_sp = 0;
};

#endif
