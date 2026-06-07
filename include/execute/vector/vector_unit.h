#ifndef RV_TCORE_VECTORUNIT_H
#define RV_TCORE_VECTORUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"

class VectorUnit : public IExecute {
    public:
    VectorUnit(const VectorConfig& conf, const VectorDeps& deps);
    ~VectorUnit() override = default;
};

#endif
