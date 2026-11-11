#include <cassert>
#include <cstdlib>
#include <memory>

#include "decode/decode_unit.h"
#include "tools/queue.h"
#include "isa/opcode.h"

// DecodeUnit 内部为 d1（发射 imm）再 d0（取指包）；与下游队列组合时的步进：
//   ① fetch_decode.tick()：把上周起写入的 FetchPacket 送到读侧
//   ② decode.tick()：从 fetch_decode 读侧取指，写入 decode_rename 写侧
//   ③ decode_rename.tick()：把微指令送到读侧供 Rename 消费

static FetchPacket make_packet_addi_x0(pc_t pc, insn_t insn)
{
    FetchPacket pkt {};
    pkt.valid = true;
    pkt.base_pc = pc;
    pkt.valid_mask = 1u;
    FetchInst fi {};
    fi.valid = true;
    fi.pc = pc;
    fi.insn = insn;
    fi.fetch_id = 1;
    fi.size = 4;
    pkt.insts.push_back(fi);
    return pkt;
}

static FetchPacket make_packet_branch(pc_t pc, insn_t insn, bool pred_taken)
{
    FetchPacket pkt {};
    pkt.valid = true;
    pkt.base_pc = pc;
    pkt.valid_mask = 1u;
    pkt.br_mask = 1u;
    pkt.pred_taken = pred_taken;
    FetchInst fi {};
    fi.valid = true;
    fi.pc = pc;
    fi.insn = insn;
    fi.fetch_id = 2;
    fi.size = 4;
    pkt.insts.push_back(fi);
    return pkt;
}


int main()
{
    using namespace std;
    auto& log = LogSystem::instance();
    log.set_level(LogLevel::LEVEL3);
    log.add_writer(make_shared<ConsoleSink>());


    ASyncQueue<FetchPacket> fetch_decode_q(32);
    ASyncQueue<MInstPtr> decode_rename_q(32);
    FlushEvent flush_event {};

    DecodeDeps deps {};
    deps.fetch_decode_q = &fetch_decode_q;
    deps.decode_rename_q = &decode_rename_q;
    deps.flush_event = &flush_event;

    DecodeConfig conf {};
    conf.width = 4;
    DecodeUnit decode(conf, deps);
    decode.build();

    const insn_t addi_x0_x0_0 = 0x00000013u;
    FetchPacket pkt = make_packet_addi_x0(0x1000, addi_x0_x0_0);

    // 周期 0：输入进入 fetch_decode 写侧，tick 后读侧可见；decode 第一次 tick 只做 d0，微指令留在内部 d0_pkt
    assert(fetch_decode_q.write(pkt));
    fetch_decode_q.tick();
    assert(decode_rename_q.empty());
    decode.tick();
    assert(decode_rename_q.empty());

    // 周期 1：无新取指包；d1 把上一周期 d0_pkt 推到 decode_rename 写侧
    fetch_decode_q.tick();
    decode.tick();
    assert(decode_rename_q.empty());

    decode_rename_q.tick();
    assert(decode_rename_q.rsize() == 1);

    MInstPtr out { nullptr };
    assert(decode_rename_q.read(out));
    assert(out != nullptr);
    assert(out->valid);
    assert(out->opcode == OP_ADDI);
    assert(out->pc == 0x1000);
    assert(out->raw_inst == addi_x0_x0_0);
    delete out;

    {
        ASyncQueue<FetchPacket> blocked_fetch_decode_q(32);
        ASyncQueue<MInstPtr> blocked_decode_rename_q(1);
        FlushEvent blocked_flush_event {};
        DecodeDeps blocked_deps {};
        blocked_deps.fetch_decode_q = &blocked_fetch_decode_q;
        blocked_deps.decode_rename_q = &blocked_decode_rename_q;
        blocked_deps.flush_event = &blocked_flush_event;
        DecodeUnit blocked_decode(conf, blocked_deps);

        FetchPacket blocked_pkt = make_packet_addi_x0(0x2000, addi_x0_x0_0);
        assert(blocked_fetch_decode_q.write(blocked_pkt));
        blocked_fetch_decode_q.tick();
        blocked_decode.tick();

        MInstPtr blocker = new MicroOp();
        blocker->valid = true;
        assert(blocked_decode_rename_q.write(blocker));
        blocked_decode.tick();

        blocked_decode_rename_q.clear();
        delete blocker;

        blocked_decode.tick();
        blocked_decode_rename_q.tick();

        MInstPtr recovered { nullptr };
        assert(blocked_decode_rename_q.read(recovered));
        assert(recovered != nullptr);
        assert(recovered->valid);
        assert(recovered->opcode == OP_ADDI);
        assert(recovered->pc == 0x2000);
        delete recovered;
    }

    {
        ASyncQueue<FetchPacket> branch_fetch_decode_q(32);
        ASyncQueue<MInstPtr> branch_decode_rename_q(32);
        FlushEvent branch_flush_event {};
        DecodeDeps branch_deps {};
        branch_deps.fetch_decode_q = &branch_fetch_decode_q;
        branch_deps.decode_rename_q = &branch_decode_rename_q;
        branch_deps.flush_event = &branch_flush_event;
        DecodeUnit branch_decode(conf, branch_deps);

        const insn_t beq_x0_x0_0 = 0x00000063u;
        FetchPacket branch_pkt = make_packet_branch(0x3000, beq_x0_x0_0, true);
        assert(branch_fetch_decode_q.write(branch_pkt));
        branch_fetch_decode_q.tick();
        branch_decode.tick();
        branch_decode.tick();
        branch_decode_rename_q.tick();

        MInstPtr branch_out { nullptr };
        assert(branch_decode_rename_q.read(branch_out));
        assert(branch_out != nullptr);
        assert(branch_out->valid);
        assert(branch_out->opcode == OP_BEQ);
        assert(branch_out->pred_taken);
        delete branch_out;
    }

    return EXIT_SUCCESS;
}
