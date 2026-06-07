#ifndef RV_TCORE_TENSORUNIT_H
#define RV_TCORE_TENSORUNIT_H

#include "intf.h"
#include "module_comm.h"
#include "config.h"

class TensorUnit {
    public:
    TensorUnit(const TensorConfig& conf, const TensorDeps& deps);
    ~TensorUnit() override = default;
};

#endif
