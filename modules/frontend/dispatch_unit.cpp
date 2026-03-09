#include "dispatch_unit.h"

void DispatchUnit::set_rename_dispatch_q(ASyncQueue<DynInst>* q) {
    m_rename_dispatch_q = q;
}

void DispatchUnit::set_dispatch_issue_q(ASyncQueue<DynInst>* q) {
    m_dispatch_issue_q = q;
}

DispatchUnit::DispatchUnit(const std::string& n)
    : IDispatch(n)
{
}

void DispatchUnit::push_from_rename(const DynInst& di)
{
    m_q.push_back(di);
}

bool DispatchUnit::has_ready() const
{
    return !m_q.empty();
}

bool DispatchUnit::try_issue_one(DynInst& out)
{
    if (m_q.empty())
        return false;
    out = m_q.front();
    m_q.pop_front();
    return true;
}

void DispatchUnit::tick()
{
    // 当前实现将 dispatch 建模为“按需拉取”：
    // 实际的发射由下游通过 try_issue_one() 主动完成，这里无需额外逻辑。
}

void DispatchUnit::commit()
{
    // 暂无需要在 commit 阶段更新的状态
}
