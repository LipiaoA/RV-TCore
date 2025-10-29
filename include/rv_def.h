#include <cstdint>

constexpr uint32_t PAGE_SIZE = 4096;

using pc_t = uint64_t;
using insn_t = uint64_t;
using bytes_t = std::vector<uint8_t>;

using u64 = uint64_t;
using u8  = uint8_t;