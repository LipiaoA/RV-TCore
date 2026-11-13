#ifndef STORE_BUFFER_H
#define STORE_BUFFER_H

#include <array>
#include "module_comm.h"

struct SBEntry {
    bool     valid { false };
    bool     writing { false };   // 已发往 D$，等 resp
    uint64_t store_id { 0 };      // 调试 / 排序
    uint64_t vaddr { 0 };
    uint64_t paddr { 0 };         // forward / 写出用 PA
    uint64_t data { 0 };
    u32      width { 0 };
    u8       byte_mask { 0 };     // gen_byte_mask(paddr, width)
    u64      epoch { 0 };
};

// 从最年轻的重叠 store 往回看的结果。full_cover 才能直接转发。
struct SBForward {
    bool     hit { false };
    bool     full_cover { false };
    uint64_t addr { 0 };
    uint64_t data { 0 };
    u32      width { 0 };
    uint64_t store_id { 0 };
    u8       byte_mask { 0 };
};

class StoreBuffer {
    static constexpr size_t MAX_STORE_BUFFER_SIZE = 64;
public:
    explicit StoreBuffer(size_t capacity = 16);
public:
    bool                                        is_full() const;
    bool                                        is_empty() const;

    SBEntry&                                    front();
    const SBEntry&                              front() const;
    SBEntry&                                    back();
    const SBEntry&                              back() const;
    size_t                                      size() const;
    size_t                                      head() const;
    size_t                                      tail() const;
    size_t                                      capacity() const;
    void                                        set_capacity(size_t capacity);
    void                                        clear();
    void                                        print() const;

    void                                        push(const SBEntry& entry);
    void                                        pop();

    // 最年轻优先。addr 可与 vaddr 或 paddr 的 8B 块对齐比较。
    SBForward                                   find_youngest(uint64_t addr, u8 load_mask) const;
private:
    static u8                                   default_byte_mask(uint64_t addr, u32 width);

    std::array<SBEntry, MAX_STORE_BUFFER_SIZE>  m_entries; // 环形队列，FIFO 顺序写出
    size_t                                      m_head { 0 };            // 最老，下一个写出
    size_t                                      m_tail { 0 };            // 分配端
    size_t                                      m_size { 0 };
    size_t                                      m_capacity { 16 };
};
#endif
