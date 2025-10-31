#include <iostream>
#include "branch_predictor.h"

struct TraceEntry { uint64_t pc; bool taken; };

void simulate(HybridPredictor &bp, const vector<TraceEntry> &trace) {
    for (auto &t : trace) {
        bool lp, gp;
        bool pred = bp.predict(t.pc, lp, gp);
        bp.update(t.pc, t.taken);
    }
    cout << "Accuracy: " << bp.accuracy()*100 << "%\n";
    cout << "Local Accuracy: " << bp.local_acc()*100 << "%\n";
    cout << "Global Accuracy: " << bp.global_acc()*100 << "%\n";
}

int main() {
    // 参数：BHT大小、BHR长度、PC低位数、GHR长度
    size_t bht_entries = 2048;
    unsigned bhr_bits = 6;
    unsigned pc_low_bits = 6;
    unsigned ghr_bits = 8;
    bool chooser_table = false;

    HybridPredictor bp(bht_entries, pc_low_bits, bhr_bits, ghr_bits, chooser_table);

    // 构造简单trace
    vector<TraceEntry> trace;
    uint64_t pc0 = 0x1000, pc1 = 0x2000;
    for (int i=0; i<1000; i++) {
        trace.push_back({pc0, true});
        trace.push_back({pc1, (i%2)==0});
    }

    simulate(bp, trace);
    return 0;
}
