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

class Core {
public:
    explicit Core(const CoreDeps & deps);
    virtual ~Core();

public:
    void                                    build();
    void                                    init(pc_t start_pc);
    void                                    tick();
    void                                    reset();
    void                                    commit();
    bool                                    exit() const;

private:
    FetchUnit&                              m_fetch;
    DecodeUnit&                             m_decode;
    RenameUnit&                             m_rename;
    IssueUnit&                              m_issue;
    RegFileUnit&                            m_regfile;
    ExecuteUnit&                            m_execute;
    CommitUnit&                             m_commit;
    CycleScheduler&                         m_sched;
    bool                                    m_exit { false };
};

#endif // TCORE_CORE_H
