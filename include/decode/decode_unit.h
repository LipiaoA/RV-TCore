
#ifndef RV_TCORE_DECODEUNIT_H
#define RV_TCORE_DECODEUNIT_H

#include <deque>
#include <vector>

#include "intf.h"
#include "decode/inst.h"
#include "module_comm.h"
#include "config.h"

class FetchUnit;

class DecodeUnit : public IDecode {
public:
    DecodeUnit(const DecodeConfig& conf, const DecodeDeps& deps);
    ~DecodeUnit() {}
public:
    void                                tick() override;
    void                                commit() override;
    void                                build() override {}

    bool                                has_decoded() const;
    bool                                try_pop_decoded(MicroOp& out);

    static void                         predecode(insn_t raw, PredecodeInfo* out);
    static void                         predecode_one(insn_t raw, PredecodeInfo* out) { predecode(raw, out); }

private:
    void                                d0();
    void                                d1();

    bool                                decode(insn_t raw, MicroOp *out);
    i64                                 extract_imm(u64 raw, InstFmt fmt, InstOp op);

private:
    FetchUnit*                          m_fetch { nullptr };
    u32                                 rv64i_insts_size;
    std::deque<MicroOp>                 m_decoded_q;
    size_t                              m_decode_width { 4 };

    std::vector<MicroOp>                m_d0_pkt;

    ASyncQueue<FetchPacket>&            m_fetch_decode_q;
    ASyncQueue<MicroOp>&                m_decode_rename_q;

    u64                                 m_next_decode_id { 0 };
    bool                                m_stall { false };
};

#endif
