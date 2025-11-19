#include <iostream>

#include "elf_l.h"
#include "rv_def.h"

bool ELFReader::elf_valid() {
    if (m_fsiz < sizeof(Elf64_Ehdr)) {
        fprintf(stderr, "file too small\n");
        return false;
    }

    if (memcmp(m_ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "not an ELF file\n");
        return false;
    }
    if (m_ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "only ELF64 supported in this model\n");
        return false;
    }
    if (m_ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "only little-endian supported\n");
        return false;
    }
    if (m_ehdr->e_machine != EM_RISCV) {
        fprintf(stderr, "only support riscv elf\n");
        return false;
    }
    return true;
}

bool ELFReader::read(Memory& mem) {

    m_phdrs = (const Elf64_Phdr*)((const uint8_t*)m_base + m_ehdr->e_phoff);
    m_shdrs = (const Elf64_Shdr*)((const uint8_t*)m_base + m_ehdr->e_shoff);

    m_entry = m_ehdr->e_entry;
    std::cout << "Entry: 0x" << std::hex << m_entry << std::endl;

    // Program Headers
    const size_t phoff = static_cast<size_t>(m_ehdr->e_phoff);
    const size_t phentsize = static_cast<size_t>(m_ehdr->e_phentsize);
    const size_t phnum = static_cast<size_t>(m_ehdr->e_phnum);

    for (int i = 0; i < m_ehdr->e_shnum; i++) {
        const Elf64_Phdr & phdr = m_phdrs[i];

        if (phdr.p_type != PT_LOAD) continue;

        const u64 vaddr = phdr.p_vaddr;           // 该段在内存中的虚拟地址
        const u64 file_siz = phdr.p_filesz;       // elf 中该段大小
        const u64 mem_siz = phdr.p_memsz;         // 内存中该段大小
        const u64 file_offset = phdr.p_offset;    // 该段在文件中的偏移(基于文件头开始算)
        std::cout << "vaddr: 0x" << std::hex << vaddr     << std:: endl
                  << "fsize: " << std::dec << file_siz << std:: endl
                  << "msize: " << std::dec << mem_siz  << std:: endl;

        if (phoff + phentsize * phnum > m_fsiz) {
            throw std::runtime_error("Program headers exceed file size");
        }
        const u64 alloc_size = round_up(mem_siz, PAGE_SIZE);
        mem.add_segment(vaddr, alloc_size);

        if (file_siz > 0) {
            mem.write_block(vaddr, (void *)((u64)m_base + file_offset), static_cast<size_t>(file_siz));
        }
        // 清 BSS
        if (mem_siz > file_siz) {
            const u64 bss_addr = vaddr + file_siz;
            u64 bss_len = mem_siz - file_siz;

            // 按页块清零，避免大段内存反复分配
            static uint8_t zero_page[PAGE_SIZE] = {0};
            while (bss_len > 0) {
                size_t chunk = static_cast<size_t>(std::min<u64>(bss_len, PAGE_SIZE));
                mem.write_block(bss_addr + (mem_siz - file_siz - bss_len), zero_page, chunk);
                bss_len -= chunk;
            }
        }
    }
    return true;
}

std::vector<std::pair<u64, u64>> ELFReader::get_load_segments()
{
    std::vector<std::pair<u64, u64>> ret;
    for (int i = 0; i < m_ehdr->e_phnum; i++) {
        const Elf64_Phdr &p = m_phdrs[i];
        if (p.p_type == PT_LOAD) {
            ret.emplace_back(p.p_vaddr, p.p_memsz);
        }
    }
    return ret;
}
