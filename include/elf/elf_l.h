#ifndef RV_TCORE_ELF_H
#define RV_TCORE_ELF_H

#include <elf.h>
#include "memory.h"

class ELFReader {

public:
    ELFReader(const void* b, size_t s) : m_base(b), m_fsiz(s), m_ehdr(b) {}
    ELFReader() = delete;

public:
    int find_section_by_name(const char* nm);
    vector<pair<uint64_t,uint64_t>> get_load_segments()
    bool read(Memory &mem);

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