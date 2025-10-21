#ifndef RV_TCORE_ELF_H
#define RV_TCORE_ELF_H

#include <elf.h>
#include "memory.h"

class ELFReader {
public:
    const void* m_base = nullptr; // mmap'd file
    size_t m_fsiz = 0;

    const Elf64_Ehdr* m_ehdr = nullptr;
    const Elf64_Phdr* m_phdrs = nullptr;
    const Elf64_Shdr* m_shdrs = nullptr;
    const char* m_shstrtab = nullptr;
    const char* m_strtab = nullptr; // first .strtab found
    const Elf64_Sym* m_symtab = nullptr;
    size_t m_symtab_count = 0;
    uint64_t m_entry = 0;

    ELFReader(const void* b, size_t s) : m_base(b), m_fsiz(s), m_ehdr(b) {}
    ELFReader() {}

private:
    bool elf_valid();

public:

    int find_section_by_name(const char* nm);

    vector<pair<uint64_t,uint64_t>> get_load_segments()
    bool read(Memory &mem);

    static constexpr uint64_t PAGE_SIZE = 4096;
    static uint64_t page_round_up(uint64_t v) {
        return (v + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }

};

#endif