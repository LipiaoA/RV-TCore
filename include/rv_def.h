#ifndef RV_TCORE_RVDEF_H
#define RV_TCORE_RVDEF_H


#include <cstdint>
#include <vector>
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8  = uint8_t;

constexpr u32 PAGE_SIZE = 4096;

using pc_t = u64;
using insn_t = u64;
using bytes_t = std::vector<u8>;



#endif