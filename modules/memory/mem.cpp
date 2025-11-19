#include "mem.h"

Memory::Memory(bool check_bound) : m_checkBound(check_bound) {}
Memory::~Memory() {}

void Memory::add_segment(u64 base, u64 size) {
    // 检查是否与现有分片冲突
    for (auto &seg : m_segments) {
        if (!(base + size <= seg.base || seg.base + seg.size <= base)) {
            throw std::runtime_error("Memory segment overlap at base=0x" + to_hex(base));
        }
    }
    m_segments.emplace_back(base, size);
}


u64 Memory::load(u64 addr, int size) const {
    auto seg = find_segment(addr, size);
    u64 off = seg->offset(addr);
    u64 val = 0;
    for (int i = 0; i < size; i++) {
        val |= static_cast<u64>(seg->storage[off + i]) << (8 * i);
    }
    return val;
}

std::vector<u8> Memory::load_cache_line(u64 addr, u32 size) const
{
    using namespace std;
    auto seg = find_segment(addr, size);
    u64 off = seg->offset(addr);
    auto start = seg->storage.begin() + off;
    auto end = start + size;
    auto res = vector<u8>(start, end);
    return res;
}

void Memory::store(u64 addr, u64 value, int size) {
    auto seg = find_segment(addr, size);
    u64 off = seg->offset(addr);
    for (int i = 0; i < size; i++) {
        seg->storage[off + i] = static_cast<u8>((value >> (8 * i)) & 0xFF);
    }
}

void Memory::write_block(u64 addr, const void* data, size_t len) {
    auto seg = find_segment(addr, len);
    u64 off = seg->offset(addr);
    std::memcpy(&seg->storage[off], data, len);
}

u8* Memory::raw_ptr(u64 addr) {
    auto seg = find_segment(addr, 1);
    return &seg->storage[seg->offset(addr)];
}

void Memory::dump_segments() const {
    for (auto &seg : m_segments) {
        printf("[Segment] base=0x%016llx size=%llu bytes\n",
                (unsigned long long)seg.base,
                (unsigned long long)seg.size);
    }
}

const Segment* Memory::find_segment(u64 addr, u64 len) const {
    for (auto &seg : m_segments) {
        if (seg.contains(addr) && seg.contains(addr + len - 1)) {
            return &seg;
        }
    }
    if (m_checkBound)
        throw std::out_of_range("Memory access out of mapped range: addr=0x" + to_hex(addr));
    return nullptr;
}

Segment* Memory::find_segment(u64 addr, u64 len) {
    for (auto &seg : m_segments) {
        if (seg.contains(addr) && seg.contains(addr + len - 1)) {
            return &seg;
        }
    }
    if (m_checkBound)
        throw std::out_of_range("Memory access out of mapped range: addr=0x" + to_hex(addr));
    return nullptr;
}

