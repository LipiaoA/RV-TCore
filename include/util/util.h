
#ifndef TCORE_UTIL_H
#define TCORE_UTIL_H

#include <string>
#include <cstdint>
#include <cstring>
#include <cstdint>
#include <map>
#include <sys/mman.h>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include <cassert>

#include "rv_def.h"
#include "log.h"

std::string to_hex(u64 x);
void hexdump(const void* p, size_t n);

inline bool is_little_endian() {
    uint16_t x = 1;
    return *(uint8_t*)&x == 1;
}

typedef struct MappedFile {
    int fd = -1;
    size_t size = 0;
    void *data = MAP_FAILED;

    bool map(const char* path);
    void unmap();
} MappedFile;

template <typename T>
typename std::enable_if<!std::is_array<T>::value>::type
delete_ptr(T*& ptr) {
    if (!ptr) return;
    delete ptr;
    ptr = nullptr;
}

template <typename T>
typename std::enable_if<std::is_array<T>::value>::type
delete_ptr(T*& ptr) {
    if (!ptr) return;
    delete[] ptr;
    ptr = nullptr;
}

u64 round_up(u64 v, u64 size);
u64 round_down(u64 v, u64 size);

class CycleScheduler {
public:
    u64 cycle() const { return m_cycle; }
    void schedule_after(u64 delay, std::function<void()> cb) {
        u64 t = m_cycle + delay;
        LOG_LEVEL1("schedule", "dealy:%ld, exp m_cycle: %ld \n", delay, t);
        m_events[t].push_back(move(cb));
    }
    void tick() {
        ++m_cycle;
        auto it = m_events.find(m_cycle);
        if (it != m_events.end()) {
            auto tasks = move(it->second);
            m_events.erase(it);
            LOG_LEVEL1("schedule tick", "run: %ld \n", m_cycle);
            for (auto &task : tasks) task();
        }
    }
    void run_cycles(u64 n) { for (u64 i = 0; i < n; ++i) tick(); }

private:
    u64 m_cycle = 0;
    std::map<u64, std::vector<std::function<void()>>> m_events;
};

// ------------------------- SimpleFuture --------------------------------
// Minimal future with .then continuation (no blocking get)
template<typename T>
class SimpleFuture {
    struct State {
        std::mutex mtx;
        bool ready=false;
        T val;
        std::vector<std::function<void(const T&)>> cbs;
    };

public:
    SimpleFuture() : m_state(std::make_shared<State>()) {}
    bool ready() const { std::lock_guard<std::mutex> lk(m_state->mtx); return m_state->ready; }
    void set_value(const T &v) {
        std::vector<std::function<void(const T&)>> cbs;
        {
            std::lock_guard<std::mutex> lk(m_state->mtx);
            if (m_state->ready) return;
            m_state->val = v;
            m_state->ready = true;
            cbs = move(m_state->cbs);
        }
        for (auto &cb : cbs) cb(m_state->val);
    }
    void then(std::function<void(const T&)> cb) {
        bool call_now = false;
        T copy;
        {
            std::lock_guard<std::mutex> lk(m_state->mtx);
            if (m_state->ready) { call_now = true; copy = m_state->val; }
            else m_state->cbs.push_back(move(cb));
        }
        if (call_now) cb(copy);
    }
private:

    std::shared_ptr<State> m_state;
};


static inline u64 mask_low(u64 v, int b) { return v & ((1ull<<b)-1); }
static inline u64 xor_fold(u64 x, int out_bits) {
    u64 res = 0;
    for (int i = 0; i < 64; i += out_bits) {
        res ^= ((x >> i) & ((1ull<<out_bits)-1));
    }
    return res & ((1ull<<out_bits)-1);
}

static inline int64_t sign_extend(uint64_t value, int bits) {
    if (bits < 64) {
        return static_cast<int64_t>((value << (64 - bits)) >> (64 - bits));
    }
    return static_cast<int64_t>(value);
}

template<typename T, size_t MaxN>
class FixedStorage {
public:
    template<typename Config>
    void init(size_t n, const Config& cfg) {
        assert(n <= MaxN);
        size_ = n;
        for (size_t i = 0; i < size_; i++)
            new (&data_[i]) T(cfg);
    }

    T& operator[](size_t i) { return data_[i]; }
    size_t size() const { return size_; }

private:
    size_t size_ = 0;
    std::aligned_storage_t<sizeof(T), alignof(T)> raw_[MaxN];
    T* data_ = reinterpret_cast<T*>(raw_);
};
#endif