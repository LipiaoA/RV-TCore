
#ifndef RV_TCORE_ISSUEUNIT_H
#define RV_TCORE_ISSUEUNIT_H

#include "intf.h"

class IssueUnit : public IIssue {
public:
    IssueUnit(const std::string& n) : IIssue(n) {}
    ~IssueUnit() {}
public:
    void tick() {}
};

#endif