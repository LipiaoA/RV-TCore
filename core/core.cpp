#include "core.h"
#include "branch_predictor.h"
#include "rob.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "branch_predictor.h"
#include "log.h"


Core::Core(const CoreDeps &deps)
 : m_fetch(*deps.fetch),
   m_decode(*deps.decode),
   m_execute(*deps.execute),
   m_issue(*deps.issue),
   m_rename(*deps.rename),
   m_rob(*deps.rob),
   m_commit(*deps.commit)
{ }

Core::~Core() { }

void Core::tick() {
    m_fetch.tick();
    m_commit.tick();
    m_decode.tick();
    m_execute.tick();
    m_issue.tick();
    m_rename.tick();
    m_rob.tick();
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
}

