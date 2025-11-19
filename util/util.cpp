#include "util.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


/*
 * 业务无关代码
 */

std::string to_hex(uint64_t x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)x);
    return std::string(buf);
}

void hexdump(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        if (i % 16 == 0) printf("%08zx  ", i);
        printf("%02x ", b[i]);
        if (i % 16 == 15) printf("\n");
    }
    if (n % 16) printf("\n");
}

bool MappedFile::map(const char* path) {
    struct stat st;
    fd = open(path, O_RDONLY);

    if (fd < 0) return false;
    if (fstat(fd, &st) < 0) return false;
    size = st.st_size;

    data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    return data != MAP_FAILED;
}


void MappedFile::unmap() {
    if (data != MAP_FAILED) munmap(data, size);
    if (fd > 0) close(fd);
    data = MAP_FAILED;
    fd = -1;
    size = 0;
}

u64 round_up(u64 v, u64 size) {
    return (v + size - 1) & ~(size - 1);
}

u64 round_down(u64 value, u64 alignment) {
    return value & ~(alignment - 1);
}

