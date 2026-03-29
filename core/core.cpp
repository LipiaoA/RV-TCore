#include "core.h"
#include "branch_predictor.h"
#include "rob.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "dispatch_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "branch_predictor.h"
#include "log.h"


Core::Core(const CoreDeps &deps)
 : m_ifetch(*deps.fetch),
   m_decode(*deps.decode),
   m_rename(*deps.rename),
   m_issue(*deps.issue),
   m_execute(*deps.execute),
   m_rob(*deps.rob),
   m_commit(*deps.commit) {

}

Core::Core() {}

Core::~Core() = default;

void Core::tick() {
    ICore::tick();
    for (auto& q : m_queues)
        q->tick();
}

void Core::build()
{
    m_fetch.build();
    m_commit.build();
    m_decode.build();
    m_execute.build();
    m_issue.build();
    m_rename.build();
    m_rob.build();
    add_module(&m_ifetch);
    add_module(&m_commit);
    add_module(&m_decode);
    add_module(&m_execute);
    add_module(&m_issue);
    add_module(&m_rename);
    add_module(&m_rob);
}

