#ifndef RV_TCORE_RVDEF_H
#define RV_TCORE_RVDEF_H


#include <cstdint>
#include <vector>
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8  = uint8_t;

using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8  = int8_t;

constexpr u32 PAGE_SIZE = 4096;

using pc_t = u64;
using insn_t = u64;
using bytes_t = std::vector<u8>;

#define INPUT
#define OUTPUT

// 内存分配 MAX 配置大小定义
constexpr u32 MAX_ROB_ENTRIES = 512;
constexpr u32 MAX_PRF_ENTRIES = 512;
constexpr u32 MAX_IQ_ENTRIES = 32;
constexpr u32 MAX_FTQ_ENTRIES = 8;

constexpr u32 MAX_FETCH_WIDTH = 8;


#endif