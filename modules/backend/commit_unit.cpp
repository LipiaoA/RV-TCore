#include "commit_unit.h"
#include "rob/rob.h"
#include "rename/rename_unit.h"

CommitUnit::CommitUnit(const CommitConfig& conf, const CommitDeps& deps)
    : ICommit()
    , m_execute_commit_q(*deps.execute_commit_q)
    , m_rob(*deps.rob)
    , m_rename(*deps.rename)
    , m_commit_width(conf.width)
{}

void CommitUnit::build() {}

void CommitUnit::tick() {}

void CommitUnit::commit() {
    while (true) {
        auto opt = m_rob.try_commit_head();
        if (!opt.has_value())
            break;

        const RobEntry& e = opt.value();
        if (e.old_phys_rd >= 0)
            m_rename.recycle_phys(e.old_phys_rd);
    }
}
