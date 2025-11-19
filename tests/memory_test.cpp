
#include <iostream>

#include "mem.h"

int main() {
    Memory mem;

    // 添加分片，比如：ROM、RAM、MMIO
    mem.add_segment(0x00000000, 0x1000);   // ROM  4KB
    mem.add_segment(0x80000000, 0x100000); // RAM  1MB
    mem.add_segment(0x10000000, 0x1000);   // MMIO 4KB

    mem.dump_segments();

    // 在 RAM 中写入测试数据
    mem.store32(0x80000000, 0xDEADBEEF);
    std::cout << std::hex << "Read from RAM: 0x"
              << mem.load32(0x80000000) << "\n";

    // 在 ROM 中写入字符串
    const char msg[] = "Hello Segment";
    mem.write_block(0x00000010, msg, sizeof(msg));
    std::cout << "Read from ROM: "
              << (char*)mem.raw_ptr(0x00000010) << "\n";

    // 超范围访问会抛异常
    try {
        mem.load32(0x90000000);
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
