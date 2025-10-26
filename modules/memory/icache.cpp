#include "icache.h"

vector<SimpleFuture<vector<u8>>> fetch_instructions_async(u64 vaddr, size_t inst_size, size_t fetch_count)
{
    vector<SimpleFuture<vector<u8>>> outs; outs.reserve(fetch_count);
    for (size_t i=0;i<fetch_count;++i) {
        u64 cur_va = vaddr + i*inst_size;
        SimpleFuture<vector<u8>> inst_fut;
        auto tfut = m_tlb->translate_async(cur_va);
        tfut.then([this, cur_va, inst_size, inst_fut](const TLBResult &res) mutable {
            if (!res.ok) {
                // fault: return empty vector (caller can interpret as fault)
                vector<u8> empty; inst_fut.set_value(empty); return;
            }
            u64 pa = res.pa; u64 line_addr = align_down(pa, this->m_cfg.line_size); size_t offset = pa - line_addr;
            if (offset + inst_size <= this->m_cfg.line_size) {
                auto lf = this->fetch_line_async(line_addr);
                lf.then([offset, inst_size, inst_fut](const vector<u8>& line){
                    vector<u8> inst(inst_size); memcpy(inst.data(), line.data()+offset, inst_size); inst_fut.set_value(inst);
                });
            } else {
                u64 next_line = line_addr + this->m_cfg.line_size;
                auto lf1 = this->fetch_line_async(line_addr);
                auto lf2 = this->fetch_line_async(next_line);
                lf1.then([lf2, offset, inst_size, inst_fut, this](const vector<u8>& l1) mutable {
                    lf2.then([l1, offset, inst_size, inst_fut, this](const vector<u8>& l2) mutable {
                        vector<u8> inst(inst_size); size_t part1 = this->m_cfg.line_size - offset;
                        memcpy(inst.data(), l1.data()+offset, part1); memcpy(inst.data()+part1, l2.data(), inst_size-part1);
                        inst_fut.set_value(inst);
                    });
                });
            }
        });
        outs.push_back(inst_fut);
    }
    return outs;
}