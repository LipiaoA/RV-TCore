
#ifndef RV_TCORE_COMMITUNIT_H
#define RV_TCORE_COMMITUNIT_H

#include "intf.h"

class CommitUnit  : public ICommit {
public:
    CommitUnit(const std::string& n) : ICommit(n) {}
    ~CommitUnit() {}
public:
    void                                tick() {}
    void                                commit() {}
};

#endif