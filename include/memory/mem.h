#ifndef RV_MEMORY_H
#define RV_MEMORY_H

#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstdio>

#include "intf.h"
#include "util.h"

typedef struct Segment {
    u64 base;                 // 分片基地址
    u64 size;                 // 分片大小
    std::vector<u8> storage;  // 实际存储

    Segment(u64 b, u64 s)
        : base(b), size(s), storage(s, 0) {}

    inline bool contains(u64 addr) const {
        return (addr >= base) && (addr < base + size);
    }

    inline u64 offset(u64 addr) const {
        return addr - base;
    }
} Segment;

class Memory {
public:
    explicit Memory(bool check_bound = true);
    ~Memory();

public:
    void                                        add_segment(u64 base, u64 size);
    void                                        write_block(u64 addr, const void* data, size_t len);

    // 快捷接口
    inline u8                                   load8 (u64 addr) const { return load(addr, 1); }
    inline u16                                  load16(u64 addr) const { return load(addr, 2); }
    inline u32                                  load32(u64 addr) const { return load(addr, 4); }
    inline u64                                  load64(u64 addr) const { return load(addr, 8); }

    inline void                                 store8 (u64 addr, u8  v) { store(addr, v, 1); }
    inline void                                 store16(u64 addr, u16 v) { store(addr, v, 2); }
    inline void                                 store32(u64 addr, u32 v) { store(addr, v, 4); }
    inline void                                 store64(u64 addr, u64 v) { store(addr, v, 8); }

    std::vector<u8>                             load_cache_line(u64 addr, u32 size) const;
    bool                                        contains(u64 addr, u64 len) const;

public:
    u8*                                         raw_ptr(u64 addr);
    void                                        dump_segments() const;
    u64                                         load(u64 addr, u32 size) const;
    void                                        store(u64 addr, u64 value, u32 size);

private:
    const Segment*                              find_segment(u64 addr, u64 len) const;
    Segment*                                    find_segment(u64 addr, u64 len);


private:

    bool                                        m_checkBound;
    std::vector<Segment>                        m_segments;
};

#endif
