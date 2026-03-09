
#ifndef RV_TCORE_DISPATCHUNIT_H
#define RV_TCORE_DISPATCHUNIT_H

#include "intf.h"
#include "decode/inst.h"
#include "module_comm.h"

class DispatchUnit : public IDispatch {
public:
    DispatchUnit(const std::string& n);
    ~DispatchUnit() {}
public:
    void                                tick() override;
    void                                commit() override;

    // 由 RenameUnit 推入已重命名的指令
    void                                push_from_rename(const DynInst& di);

    // 下游（Issue/Execute）从 DispatchUnit 取走要发射的指令
    bool                                try_issue_one(DynInst& out);
    bool                                has_ready() const;

    void                                set_rename_dispatch_q(ASyncQueue<DynInst>* q);
    void                                set_dispatch_issue_q(ASyncQueue<DynInst>* q);

private:
    INPUT  ASyncQueue<DynInst>*         m_rename_dispatch_q { nullptr };
    OUTPUT ASyncQueue<DynInst>*         m_dispatch_issue_q   { nullptr };
    std::deque<DynInst>                 m_q;
    size_t                              m_dispatch_width { 4 };
};

#endif