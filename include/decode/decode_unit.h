
#ifndef RV_TCORE_DECODEUNIT_H
#define RV_TCORE_DECODEUNIT_H

#include "intf.h"
#include "fetch_unit.h"
#include "decode/inst.h"
#include "module_comm.h"

// Decode 单元：从 FetchUnit 拉取 fetch_packet_t，解码成 SInst 流
class DecodeUnit : public IDecode {
public:
    DecodeUnit(const std::string& n);
    ~DecodeUnit() {}
public:
    void                                tick() override;
    void                                commit() override;

    // 由 Core 或上层调用，绑定上游取指单元
    void                                attach_fetch(FetchUnit* f);

    // 对下游（RenameUnit）暴露的解码结果访问接口
    bool                                has_decoded() const;
    bool                                try_pop_decoded(SInst& out);

    void                                set_fetch_decode_q(ASyncQueue<FetchPacket>* q);
    void                                set_decode_rename_q(ASyncQueue<DecodedInst>* q);

private:
    bool                                decode(u64 raw, u64 pc, SInst *out);
    i64                                 extract_imm(u64 raw, InstFmt fmt, InstOp op);

private:
    FetchUnit*                          m_fetch { nullptr };
    u32                                 rv64i_insts_size;
    // 解码后静态指令队列
    std::deque<SInst>                   m_decoded_q;
    size_t                              m_decode_width { 4 };

    INPUT ASyncQueue<FetchPacket>*      m_fetch_decode_q { nullptr };
    OUTPUT ASyncQueue<DecodedInst>*     m_decode_rename_q { nullptr };

};

#endif