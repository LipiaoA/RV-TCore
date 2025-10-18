#ifndef RV_MEMORY_H
#define RV_MEMORY_H

#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstdio>

#include "intf.h"
#include "util.h"

typedef struct Segment {
    uint64_t base;                 // 分片基地址
    uint64_t size;                 // 分片大小
    std::vector<uint8_t> storage;  // 实际存储

    Segment(uint64_t b, uint64_t s)
        : base(b), size(s), storage(s, 0) {}

    inline bool contains(uint64_t addr) const {
        return (addr >= base) && (addr < base + size);
    }

    inline uint64_t offset(uint64_t addr) const {
        return addr - base;
    }
} Segment;

class Memory {
public:


public:
    explicit Memory(bool check_bound = true);
    ~Memory();

public:
    // 添加内存分片
    void add_segment(uint64_t base, uint64_t size);
    // 批量写入
    void write_block(uint64_t addr, const void* data, size_t len);

    // 快捷接口
    inline uint8_t  load8 (uint64_t addr) const { return load(addr, 1); }
    inline uint16_t load16(uint64_t addr) const { return load(addr, 2); }
    inline uint32_t load32(uint64_t addr) const { return load(addr, 4); }
    inline uint64_t load64(uint64_t addr) const { return load(addr, 8); }

    inline void store8 (uint64_t addr, uint8_t  v) { store(addr, v, 1); }
    inline void store16(uint64_t addr, uint16_t v) { store(addr, v, 2); }
    inline void store32(uint64_t addr, uint32_t v) { store(addr, v, 4); }
    inline void store64(uint64_t addr, uint64_t v) { store(addr, v, 8); }


public:
    // 返回底层内存指针（用于调试或DMA）
    uint8_t* raw_ptr(uint64_t addr);
    // 打印分片信息（调试）
    void dump_segments() const;

private:
    const Segment* find_segment(uint64_t addr, uint64_t len) const;
    Segment* find_segment(uint64_t addr, uint64_t len);
    // 加载数据
    uint64_t load(uint64_t addr, int size) const;
    // 存储数据
    void store(uint64_t addr, uint64_t value, int size);


private:

    bool m_checkBound;
    std::vector<Segment> m_segments;
};

#endif
