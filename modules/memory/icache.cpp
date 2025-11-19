#include <bit>

#include "util.h"
#include "icache.h"
#include "log.h"

std::vector<SimpleFuture<std::vector<u8>>>
ICache::fetch_instructions_async(u64 vaddr, size_t inst_size, size_t fetch_count)
{
    std::vector<SimpleFuture<std::vector<u8>>> outs;
    outs.reserve(fetch_count);
    for (size_t i = 0; i < fetch_count; ++i) {
        u64 cur_va = vaddr + i * inst_size;
        SimpleFuture<std::vector<u8>> inst_fut;
        auto tfut = m_tlb->translate_async(cur_va);
        auto fuc = [this, cur_va, inst_size, inst_fut](const TLBResult &res) mutable {
            if (!res.ok) {
                // fault: return empty std::vector (caller can interpret as fault)
                std::vector<u8> empty;
                inst_fut.set_value(empty);
                LOG_LEVEL1("TLB", "tlb translate error !!!");
                return;
            }
            u64 pa = res.pa;
            u64 line_addr = round_down(pa, m_cfg.line_size);
            size_t offset = pa - line_addr;
            if (offset + inst_size <= m_cfg.line_size) {
                auto lf = fetch_line_async(line_addr);
                lf.then([offset, inst_size, inst_fut](const std::vector<u8>& line) mutable {
                    std::vector<u8> inst(inst_size);
                    memcpy(inst.data(), line.data()+offset, inst_size);
                    inst_fut.set_value(inst);
                });
            } else {
                u64 next_line = line_addr + m_cfg.line_size;
                auto lf1 = fetch_line_async(line_addr);
                auto lf2 = fetch_line_async(next_line);
                lf1.then([lf2, offset, inst_size, inst_fut, this](const std::vector<u8> &l1) mutable {
                    lf2.then([l1, offset, inst_size, inst_fut, this](const std::vector<u8> &l2) mutable {
                        std::vector<u8> inst(inst_size);
                        size_t part1 = m_cfg.line_size - offset;
                        memcpy(inst.data(), l1.data() + offset, part1);
                        memcpy(inst.data() + part1, l2.data(), inst_size - part1);
                        inst_fut.set_value(inst);
                    });
                });
            }
        };

        tfut.then(fuc);
        outs.push_back(inst_fut);
    }
    return outs;
}
