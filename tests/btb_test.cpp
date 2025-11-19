#include <bits/stdc++.h>

#include "intf.h"
#include "target_predictor.h"

int main()
{
    SimConfig cfg;
    TargetPredictor sim(cfg);

    // Create a synthetic trace that exercises direct branches, calls/rets, and indirects.
    std::vector<InstRecord> trace;

    // 1) Some direct branches (taken)
    for (int i = 0; i < 200; i++) {
        u64 pc = 0x1000 + i * 4;
        InstRecord r{pc, DIRECT, pc + 0x200 + (i % 8) * 4, true};
        trace.push_back(r);
    }

    // 2) Calls and corresponding rets
    // simulate calls from a few call sites
    for (int i = 0; i < 50; i++) {
        u64 call_pc = 0x2000 + (i % 5) * 4;
        InstRecord call{call_pc, CALL, 0x8000 + (i % 3) * 0x10, false};
        trace.push_back(call);
        // some body
        for (int k = 0; k < 3; k++)
            trace.push_back({call_pc + 4 + k * 4, OTHER, 0, false});
        // return
        InstRecord ret{call_pc + 16, RET, call_pc + 4, false};
        trace.push_back(ret);
    }

    // 3) Indirect branches whose targets depend on history
    // We'll craft two call targets that alternate depending on history pattern
    // pattern: if last BHR lowbit==1 -> target A else target B
    for (int i = 0; i < 200; i++) {
        u64 pc = 0x3000 + (i % 7) * 4;
        // choose target according to simple rule to make history useful
        bool history_bit = (i % 3 == 0);
        u64 target = history_bit ? 0x9000 : 0xA000;
        InstRecord ind{pc, INDIRECT, target, true};
        trace.push_back(ind);
    }

    // run simulation
    sim.run(trace);
    sim.print_stats();

    std::cout << "Done.\n";
    return 0;
}
