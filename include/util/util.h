
#ifndef TCORE_UTIL_H
#define TCORE_UTIL_H

#include <string>
#include <cstdint>
#include <cstring>
#include <cstdint>
#include <map>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

std::string to_hex(uint64_t x);
void hexdump(const void* p, size_t n);

inline bool is_little_endian() {
    uint16_t x = 1;
    return *(uint8_t*)&x == 1;
}

typedef struct MappedFile {
    int m_fd = -1;
    size_t m_size = 0;
    void *m_data = MAP_FAILED;

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

uint64_t round_up(uint64_t v, uint64_t size) {
    return (v + size - 1) & ~(size - 1);
}

class CycleScheduler {
public:
    u64 get_cycle() const { return m_cycle; }
    void schedule_after(u64 delay, std::function<void()> cb) {
        u64 t = m_cycle + delay;
        m_events[t].push_back(move(cb));
    }
    void tick() {
        ++m_cycle;
        auto it = m_events.find(m_cycle);
        if (it != m_events.end()) {
            auto tasks = move(it->second);
            m_events.erase(it);
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
    SimpleFuture() : m_state(make_shared<State>()) {}
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
    // simple xor-fold to out_bits
    u64 res = 0;
    for (int i = 0; i < 64; i += out_bits) {
        res ^= ((x >> i) & ((1ull<<out_bits)-1));
    }
    return res & ((1ull<<out_bits)-1);
}

#endif