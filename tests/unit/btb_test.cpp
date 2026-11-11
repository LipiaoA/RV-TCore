#include <cassert>
#include <cstdlib>
#include <iostream>

#include "btb.h"
#include "rv_def.h"

int main()
{
    BTB btb(256, 4, 16, 2);
    u32 time = 1;

    // Direct/cond branch target
    {
        pc_t pc = 0x1000;
        pc_t target = 0x1200;
        btb.update(pc, target, BrType::COND, time++);
        BTBResp resp = btb.lookup(pc, time++);
        assert(resp.hit);
        assert(resp.target == target);
        assert(resp.type == BrType::COND);
    }

    // Call / ret style entries
    {
        pc_t call_pc = 0x2000;
        pc_t call_tgt = 0x8000;
        btb.update(call_pc, call_tgt, BrType::CALL, time++);
        BTBResp call_resp = btb.lookup(call_pc, time++);
        assert(call_resp.hit);
        assert(call_resp.target == call_tgt);
        assert(call_resp.type == BrType::CALL);

        pc_t ret_pc = 0x8010;
        pc_t ret_tgt = 0x2004;
        btb.update(ret_pc, ret_tgt, BrType::RET, time++);
        BTBResp ret_resp = btb.lookup(ret_pc, time++);
        assert(ret_resp.hit);
        assert(ret_resp.target == ret_tgt);
        assert(ret_resp.type == BrType::RET);
    }

    // Indirect / jalr
    {
        pc_t pc = 0x3000;
        pc_t target = 0x9000;
        btb.update(pc, target, BrType::JALR, time++);
        BTBResp resp = btb.lookup(pc, time++);
        assert(resp.hit);
        assert(resp.target == target);
        assert(resp.type == BrType::JALR);

        // Same PC, newer target overwrites
        pc_t new_target = 0xA000;
        btb.update(pc, new_target, BrType::JALR, time++);
        resp = btb.lookup(pc, time++);
        assert(resp.hit);
        assert(resp.target == new_target);
    }

    // Miss path
    {
        BTBResp miss = btb.lookup(0xDEAD0000, time++);
        assert(!miss.hit);
    }

    btb.invalidate_all();
    {
        BTBResp resp = btb.lookup(0x1000, time++);
        assert(!resp.hit);
    }

    std::cout << "btb_test passed\n";
    return EXIT_SUCCESS;
}
