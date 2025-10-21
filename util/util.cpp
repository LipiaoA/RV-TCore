#include "util.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>


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
    m_fd = open(path, O_RDONLY);

    if (m_fd < 0) return false;
    if (fstat(fd, &st) < 0) return false;
    m_size = st.st_size;

    m_data = mmap(nullptr, m_size, PORT_READ, MAP_PRIVATE, fd, 0);
    return m_data != MAP_FAILED;
}


void MappedFile::unmap() {
    if (m_data != MAP_FAILED) munmap(m_data, m_size);
    if (m_fd > 0) close(m_fd);
    m_data = MAP_FAILED;
    m_fd = -1;
    m_size = 0;
}

