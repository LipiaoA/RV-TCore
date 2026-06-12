#ifndef RV_TCORE_ROB_H
#define RV_TCORE_ROB_H

#include <optional>
#include <vector>

#include "intf.h"
#include "module_comm.h"
#include "decode/inst.h"
#include "config.h"

struct RobEntry {
    bool          valid     { false };
    MInstPtr      inst      { nullptr };
    bool          mispred   { false };
    bool          ready     { false };
    u64           result    { 0ull };
    int           old_phys_rd { -1 };
    RobEntry() : valid(false), inst(nullptr), mispred(false), ready(false), result(0ull), old_phys_rd(-1) {}
};

class ROB {
    constexpr static u32 MAX_ROB_ENTRIES = 512;
public:
    explicit ROB(const ROBConfig& conf, const ROBDeps& deps);
    ~ROB() = default;
public:
    void                                build();
    void                                flush(const FlushEvent& event);
    std::optional<ROBID>                alloc(const MInstPtr inst);
    RobEntry*                           get(ROBID id);
    void                                mark_ready(ROBID id, u64 result_val);
    std::optional<RobEntry>             commit_head();
    bool                                full() const;
    bool                                has_space(u32 count = 1) const;
    bool                                empty() const;
    std::optional<RobEntry>             head();
    size_t                              size() const { return m_size; }
    void                                reset();

    // 若某一 id 当前不在有效占用窗口内，则退化为按 (direct, id) 字典序，保证全序稳定。
    bool                                 is_older_than(const ROBID& a, const ROBID& b) const;
private:
    ROBID                               next_id(ROBID id) const;

private:
    RobEntry                            m_entries[MAX_ROB_ENTRIES];
    u32                                 m_capacity;
    // 当前有效 entry 数量
    size_t                              m_size { 0 };
    // 如果非空，指向第一个有效 entry，用于提交和回收
    ROBID                               m_head { 0, 0 };
    // 指向最后一个有效 entry的下一个位置，用于分配新的 ROBID
    ROBID                               m_tail { 0, 0 };
};

#endif
