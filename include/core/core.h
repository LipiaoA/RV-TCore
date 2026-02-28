#ifndef TCORE_CORE_H
#define TCORE_CORE_H

#include <memory>
#include <vector>
#include "util.h"
#include "intf.h"
#include "module_comm.h"

class Core : public ICore {
public:
    Core(const std::string &n) : ICore(n) {}
    virtual ~Core() override;

public:
    void                                    build() override;
    void                                    tick() override;

private:
    std::vector<DevIntf*>                   m_queues;
    ModuleCommQueues                        m_comm_queues;
};

#endif // TCORE_CORE_H
