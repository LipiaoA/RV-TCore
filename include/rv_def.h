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

using f64 = double;
using f32 = float;

constexpr u32 PAGE_SIZE = 4096;

using pc_t = u64;
using insn_t = u64;
using bytes_t = std::vector<u8>;

#define INPUT
#define OUTPUT

// 内存分配 MAX 配置大小定义
constexpr u32 MAX_PRF_ENTRIES = 512;
constexpr u32 MAX_VPRF_ENTRIES = 128;
constexpr u32 MAX_TPRF_ENTRIES = 64;
constexpr u32 MAX_IQ_ENTRIES = 32;

constexpr u32 VLEN_BITS = 2048;
constexpr u32 VLEN_BYTES = VLEN_BITS / 8;
constexpr u32 MAX_VEC_REG_BYTES = VLEN_BYTES * 8;
constexpr u32 TILE_ROWS = 16;
constexpr u32 TILE_COLS = 16;
constexpr u32 TILE_ELEM_BITS = 64;

constexpr u32 MAX_FTQ_ENTRIES = 8;

constexpr u32 MAX_FETCH_WIDTH = 8;

constexpr u64  STACK_SIZE = 0x400000; // 4MB
constexpr pc_t STACK_POINTER = 0x80000000;

// system registers addresses
constexpr u32 SYS_VSTART =0x008;
constexpr u32 SYS_VXSAT  =0x009;
constexpr u32 SYS_VXRM   =0x00A;
constexpr u32 SYS_VCSR   =0x00F;
constexpr u32 SYS_VL     =0xC20;
constexpr u32 SYS_VTYPE  =0xC21;
constexpr u32 SYS_VLENB  =0xC22;

#endif
