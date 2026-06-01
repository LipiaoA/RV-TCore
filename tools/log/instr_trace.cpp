#include "instr_trace.h"

#include <iomanip>

InstrTrace& InstrTrace::instance()
{
    static InstrTrace s_trace;
    return s_trace;
}

InstrTrace::InstrTrace() = default;

InstrTrace::~InstrTrace()
{
    close();
}

bool InstrTrace::open(const std::string& path)
{
    close();
    m_out.open(path, std::ios::out | std::ios::trunc);
    m_enabled = m_out.is_open();
    return m_enabled;
}

void InstrTrace::close()
{
    if (m_out.is_open()) {
        m_out.flush();
        m_out.close();
    }
    m_enabled = false;
}

void InstrTrace::record_retire(pc_t pc, u64 result)
{
    if (!m_enabled || !m_out.is_open())
        return;

    m_out << "pc=0x" << std::hex << std::setw(16) << std::setfill('0') << pc
          << " result=0x" << std::setw(16) << std::setfill('0') << result
          << std::dec << '\n';
}

void InstrTrace::flush()
{
    if (m_out.is_open()) {
        m_out.flush();
    }
}
