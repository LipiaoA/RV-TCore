
#ifndef RV_TCORE_RENAMEUNIT_H
#define RV_TCORE_RENAMEUNIT_H

#include "intf.h"
#include "decode/inst.h"
#include "module_comm.h"

// 简化寄存器配置：32 逻辑寄存器，64 物理寄存器（含额外的重命名空间）
constexpr int kNumLogicalRegs  = 32;
constexpr int kNumPhysicalRegs = 64;

class RenameUnit : public IRename {
public:
    RenameUnit(const std::string& n);
    ~RenameUnit() {}
public:
    void                                tick() override;
    void                                commit() override;

    // 由 DecodeUnit 推入解码好的静态指令
    void                                push_from_decode(const SInst& s);
    bool                                has_renamed() const;
    bool                                try_pop_renamed(DynInst& out);

    void                                set_decode_rename_q(ASyncQueue<DecodedInst>* q);
    void                                set_rename_dispatch_q(ASyncQueue<DynInst>* q);

private:
    INPUT  ASyncQueue<DecodedInst>*     m_decode_rename_q  { nullptr };
    OUTPUT ASyncQueue<DynInst>*         m_rename_dispatch_q{ nullptr };
    // 逻辑->物理映射表
    std::vector<int>                    m_rat;          // size = kNumLogicalRegs
    // 空闲物理寄存器列表
    std::deque<int>                     m_free_list;    // 存 [kNumLogicalRegs, kNumPhysicalRegs)

    // 输入/输出队列
    std::deque<SInst>                   m_decoded_q;
    std::deque<DynInst>                 m_renamed_q;

    size_t                              m_rename_width { 4 };
};

#endif