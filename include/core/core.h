#ifndef TCORE_CORE_H
#define TCORE_CORE_H

#include <memory>
#include <vector>
#include "util.h"
#include "intf.h"
#include "module_comm.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "execute_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "rob/rob.h"
#include "fetch_unit.h"

class Core : public ICore {
public:
    explicit Core(const CoreDeps & deps);
    virtual ~Core() override;

public:
    void                                    build() override;
    void                                    tick() override;

private:
    FetchUnit&                              m_fetch;
    DecodeUnit&                             m_decode;
    RenameUnit&                             m_rename;
    IssueUnit&                              m_issue;
    ExecuteUnit&                            m_execute;
    CommitUnit&                             m_commit;
    CycleScheduler&                         m_sched;
};

#endif // TCORE_CORE_H
