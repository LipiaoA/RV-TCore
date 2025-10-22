
#ifndef TCORE_UTIL_H
#define TCORE_UTIL_H

#include <string>
#include <cstdint>
#include <cstring>
#include <cstdint>

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

#endif