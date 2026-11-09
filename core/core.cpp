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
#include "vregfile_unit.h"
#include "tregfile_unit.h"
#include "branch_predictor.h"
#include "log.h"
#include "flush_unit.h"
#include "perf/perf_def.h"

Core::Core(const CoreDeps &deps)
 : m_fetch(*deps.fetch),
   m_decode(*deps.decode),
   m_rename(*deps.rename),
   m_issue(*deps.issue),
   m_regfile(*deps.regfile),
   m_vregfile(*deps.vregfile),
   m_tregfile(*deps.tregfile),
   m_execute(*deps.execute),
   m_commit(*deps.commit),
   m_sched(*deps.sched),
   m_flush(*deps.flush),
   m_rob(*deps.rob),
   m_flush_event(*deps.flush_event),
   m_stats(*deps.perf_stats)
{ }

Core::~Core() { }

void Core::tick() {
    if (m_stats.perf()) {
        ++m_stats.cycles;
    }
    /* 上一拍产生的flush 在当前拍仲裁后 同时被其它组件看到 */
    m_flush.tick();
    if (m_flush_event.valid) {
        if (m_flush_event.type == FlushType::STOP) {
            LOG_LEVEL1("Core", "Program Completed Execution Successfully!!! PC: 0x%lx\n",
                        m_flush_event.inst_pc);
            m_exit = true;
            return;
        } else if (m_flush_event.type == FlushType::EXCEPTION) {
            LOG_LEVEL1("Core", "ERROR! Exception Occurred At PC: 0x%lx\n", m_flush_event.inst_pc);
            m_exit = true;
            return;
        }
        m_rob.flush(m_flush_event);
    }
    m_fetch.tick();
    m_decode.tick();
    m_rename.tick();
    m_regfile.tick();
    m_vregfile.tick();
    m_tregfile.tick();
    m_execute.tick();
    m_issue.tick();
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
    m_vregfile.build();
    m_tregfile.build();
    m_execute.build();
    m_commit.build();
    m_flush.build();
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
    m_vregfile.reset();
    m_tregfile.reset();
    m_execute.reset();
    m_commit.reset();
    m_flush.reset();
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
    m_vregfile.commit();
    m_tregfile.commit();
    m_execute.commit();
    m_commit.commit();
    m_flush.commit();
}
