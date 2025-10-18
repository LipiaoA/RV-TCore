#include "memory.h"

Memory::Memory(bool check_bound) : m_checkBound(check_bound) {}
Memory::~Memory() {}

void Memory::add_segment(uint64_t base, uint64_t size) {
    // 检查是否与现有分片冲突
    for (auto &seg : m_segments) {
        if (!(base + size <= seg.base || seg.base + seg.size <= base)) {
            throw std::runtime_error("Memory segment overlap at base=0x" + to_hex(base));
        }
    }
    m_segments.emplace_back(base, size);
}


uint64_t Memory::load(uint64_t addr, int size) const {
    auto seg = find_segment(addr, size);
    uint64_t off = seg->offset(addr);
    uint64_t val = 0;
    for (int i = 0; i < size; i++) {
        val |= static_cast<uint64_t>(seg->storage[off + i]) << (8 * i);
    }
    return val;
}

// 存储数据
void Memory::store(uint64_t addr, uint64_t value, int size) {
    auto seg = find_segment(addr, size);
    uint64_t off = seg->offset(addr);
    for (int i = 0; i < size; i++) {
        seg->storage[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// 批量写入
void Memory::write_block(uint64_t addr, const void* data, size_t len) {
    auto seg = find_segment(addr, len);
    uint64_t off = seg->offset(addr);
    std::memcpy(&seg->storage[off], data, len);
}

// 返回底层内存指针（用于调试或DMA）
uint8_t* Memory::raw_ptr(uint64_t addr) {
    auto seg = find_segment(addr, 1);
    return &seg->storage[seg->offset(addr)];
}

// 打印分片信息（调试）
void Memory::dump_segments() const {
    for (auto &seg : m_segments) {
        printf("[Segment] base=0x%016llx size=%llu bytes\n",
                (unsigned long long)seg.base,
                (unsigned long long)seg.size);
    }
}

const Segment* Memory::find_segment(uint64_t addr, uint64_t len) const {
    for (auto &seg : m_segments) {
        if (seg.contains(addr) && seg.contains(addr + len - 1)) {
            return &seg;
        }
    }
    if (m_checkBound)
        throw std::out_of_range("Memory access out of mapped range: addr=0x" + to_hex(addr));
    return nullptr;
}

Segment* Memory::find_segment(uint64_t addr, uint64_t len) {
    for (auto &seg : m_segments) {
        if (seg.contains(addr) && seg.contains(addr + len - 1)) {
            return &seg;
        }
    }
    if (m_checkBound)
        throw std::out_of_range("Memory access out of mapped range: addr=0x" + to_hex(addr));
    return nullptr;
}

