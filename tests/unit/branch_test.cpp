#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "bim.h"

struct TraceEntry {
    u64 pc;
    bool taken;
};

static void simulate(BIM& bp, const std::vector<TraceEntry>& trace)
{
    u64 correct = 0;
    for (const auto& t : trace) {
        const u64 ghr = bp.ghr();
        const bool pred = bp.peek(t.pc);
        if (pred == t.taken) {
            ++correct;
        }
        bp.update(t.pc, t.taken, ghr);
        bp.shift(t.taken);
    }

    const double accuracy = trace.empty()
        ? 0.0
        : static_cast<double>(correct) / static_cast<double>(trace.size());
    std::cout << "Accuracy: " << accuracy * 100.0 << "%\n";
    assert(accuracy > 0.7);
}

int main()
{
    BIM bp(2048, 8, 4);

    std::vector<TraceEntry> trace;
    const u64 pc0 = 0x1000;
    const u64 pc1 = 0x2000;
    for (int i = 0; i < 1000; ++i) {
        trace.push_back({pc0, true});
        trace.push_back({pc1, (i % 2) == 0});
    }

    simulate(bp, trace);
    return EXIT_SUCCESS;
}
