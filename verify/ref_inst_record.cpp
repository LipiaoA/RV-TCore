#include "verify/ref_inst_record.h"

RefInstRecord ref_record(u64 seq, const MicroOp& uop, const StepSnapshot& snap)
{
    RefInstRecord rec {};
    rec.seq = seq;
    rec.pc = uop.pc;
    rec.raw_inst = static_cast<u32>(uop.raw_inst);
    rec.opcode = uop.opcode;
    rec.type = uop.type;
    rec.effect = snap.effect;
    rec.rd = snap.rd;
    rec.rd_val = snap.rd_val;
    rec.mem_addr = snap.mem_addr;
    rec.mem_data = snap.mem_data;
    rec.mem_width = snap.mem_width;
    rec.next_pc = snap.next_pc;
    rec.branch_taken = snap.branch_taken;
    rec.branch_target = snap.branch_target;
    rec.exc_cause = snap.exc_cause;
    rec.exc_tval = snap.exc_tval;
    return rec;
}
