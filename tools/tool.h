#include <string>
#include <cstdint>
#include <cstring>

static std::string to_hex(uint64_t x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)x);
    return std::string(buf);
}
