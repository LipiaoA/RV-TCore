#ifndef RV_TCORE_ELF_H
#define RV_TCORE_ELF_H

#include <elf.h>
#include <vector>
#include <cstdint>
#include <utility>

#include "util.h"
#include "mem.h"

class ELFReader {

public:
    ELFReader(const void* b, size_t s) : m_base(b), m_fsiz(s){
        m_ehdr = static_cast<const Elf64_Ehdr*>(b);
    }
    ELFReader() = delete;

public:
    std::vector<std::pair<u64, u64>> get_load_segments();
    bool read(Memory &mem);
    u64 get_entry() const {
        return m_entry;
    }

private:
    bool elf_valid();

private:
    const void* m_base = nullptr; // mmap'd file
    size_t m_fsiz = 0;

    const Elf64_Ehdr* m_ehdr = nullptr;
    const Elf64_Phdr* m_phdrs = nullptr;
    const Elf64_Shdr* m_shdrs = nullptr;
    uint64_t m_entry = 0;
};

#endif