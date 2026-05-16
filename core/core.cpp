#include "core.h"
#include "branch_predictor.h"
#include "rob.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "regfile_unit.h"
#include "branch_predictor.h"
#include "log.h"

Core::Core(const CoreDeps &deps)
 : m_fetch(*deps.fetch),
   m_decode(*deps.decode),
   m_rename(*deps.rename),
   m_issue(*deps.issue),
   m_regfile(*deps.regfile),
   m_execute(*deps.execute),
   m_commit(*deps.commit),
   m_sched(*deps.sched)
{ }

Core::~Core() { }

void Core::tick() {
    m_fetch.tick();
    m_decode.tick();
    m_rename.tick();
    m_issue.tick();
    m_regfile.tick();
    m_execute.tick();
    m_commit.tick();

    m_sched.tick();
}

void Core::build()
{
    m_fetch.build();
    m_decode.build();
    m_rename.build();
    m_issue.build();
    m_regfile.build();
    m_execute.build();
    m_commit.build();
}

void Core::init(pc_t start_pc)
{
    m_fetch.redirct(start_pc);
}

void Core::reset()
{
    m_fetch.reset();
    m_decode.reset();
    m_rename.reset();
    m_issue.reset();
    m_regfile.reset();
    m_execute.reset();
    m_commit.reset();

    m_exit = false;
}

bool Core::exit() const
{
    return m_exit;
}

void Core::commit()
{
    m_fetch.commit();
    m_decode.commit();
    m_rename.commit();
    m_issue.commit();
    m_regfile.commit();
    m_execute.commit();
    m_commit.commit();
}