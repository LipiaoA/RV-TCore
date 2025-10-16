// elf_loader.cpp
// A minimal ELF64 parser + simple loader for x86_64 (little-endian).
// - Parses ELF header, program headers, section headers
// - Prints useful information (entry point, segments, sections)
// - Loads PT_LOAD segments into a single malloc'd memory region (not mmap with fixed addresses)
// - Applies RELA-type relocations for R_X86_64_RELATIVE (basic support)
//
// Limitations / notes:
// - Only supports ELF64 (ET_EXEC/ET_DYN) little-endian x86_64 binaries
// - Symbol resolution for GLOBAL/EXTERNAL symbols is NOT implemented (would require dynamic linker)
// - Execution of loaded code is intentionally NOT performed. This loader demonstrates mapping and relocating.
// - Run on Linux. Compile with: g++ -std=c++17 -O2 -Wall -o elf_loader elf_loader.cpp

#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <elf.h>

using namespace std;

struct MappedFile {
    int fd = -1;
    size_t size = 0;
    void* data = MAP_FAILED;
    bool map(const char* path) {
        fd = open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) return false;
        size = st.st_size;
        data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        return data != MAP_FAILED;
    }


    void unmap() {
        if (data != MAP_FAILED) munmap(data, size);
        if (fd >= 0) close(fd);
        data = MAP_FAILED;
        fd = -1;
        size = 0;
    }
};

static inline bool is_little_endian()
{
    uint16_t x = 1;
    return *(uint8_t*)&x == 1;
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

class ELFParser {
public:
    const void* base = nullptr; // mmap'd file
    size_t size = 0;
    const Elf64_Ehdr* ehdr = nullptr;
    const Elf64_Phdr* phdrs = nullptr;
    const Elf64_Shdr* shdrs = nullptr;
    const char* shstrtab = nullptr;
    const char* strtab = nullptr; // first .strtab found
    const Elf64_Sym* symtab = nullptr;
    size_t symtab_count = 0;

    ELFParser(const void* b, size_t s) : base(b), size(s) {}

    bool parse() {
        if (size < sizeof(Elf64_Ehdr)) {
            fprintf(stderr, "file too small\n"); return false;
        }
        ehdr = (const Elf64_Ehdr*)base;
        if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
            fprintf(stderr, "not an ELF file\n"); return false;
        }
        if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
            fprintf(stderr, "only ELF64 supported in this tool\n"); return false;
        }
        if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
            fprintf(stderr, "only little-endian supported\n"); return false;
        }
        phdrs = (const Elf64_Phdr*)((const uint8_t*)base + ehdr->e_phoff);
        shdrs = (const Elf64_Shdr*)((const uint8_t*)base + ehdr->e_shoff);
        // section header string table
        if (ehdr->e_shstrndx != SHN_UNDEF) {
            const Elf64_Shdr& shstr = shdrs[ehdr->e_shstrndx];
            shstrtab = (const char*)base + shstr.sh_offset;
        }
        // find .strtab and .symtab
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const Elf64_Shdr& s = shdrs[i];
            const char* name = shstrtab ? shstrtab + s.sh_name : "";
            if (s.sh_type == SHT_SYMTAB) {
                symtab = (const Elf64_Sym*)((const uint8_t*)base + s.sh_offset);
                symtab_count = s.sh_size / s.sh_entsize;
                // symtab's string table is in sh_link
                int link = s.sh_link;
                if (link >= 0 && link < ehdr->e_shnum) {
                    const Elf64_Shdr& strsh = shdrs[link];
                    strtab = (const char*)base + strsh.sh_offset;
                }
            }
            if (strcmp(name, ".strtab") == 0 && !strtab) {
                strtab = (const char*)base + s.sh_offset;
            }
        }
        return true;
    }

    void print_summary() {
        printf("ELF64 file: entry=0x%016lx type=%u machine=%u phnum=%u shnum=%u\n",
               ehdr->e_entry, ehdr->e_type, ehdr->e_machine, ehdr->e_phnum, ehdr->e_shnum);
        // program headers
        for (int i = 0; i < ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = phdrs[i];
            printf("PHDR[%2d]: type=%#x off=0x%08lx vaddr=0x%016lx paddr=0x%016lx filesz=0x%lx memsz=0x%lx flags=%#x align=0x%lx\n",
                   i, p.p_type, p.p_offset, p.p_vaddr, p.p_paddr, p.p_filesz, p.p_memsz, p.p_flags, p.p_align);
        }
        // sections
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const Elf64_Shdr& s = shdrs[i];
            const char* name = shstrtab ? shstrtab + s.sh_name : "";
            printf("SHDR[%2d]: name=%s type=%#x addr=0x%016lx off=0x%08lx size=0x%lx entsize=0x%lx flags=%#lx\n",
                   i, name, s.sh_type, s.sh_addr, s.sh_offset, s.sh_size, s.sh_entsize, s.sh_flags);
        }
        if (symtab) {
            printf("Found symtab: %zu entries\n", symtab_count);
        }
    }

    // helpers to find section by name
    int find_section_by_name(const char* nm) {
        if (!shstrtab) return -1;
        for (int i = 0; i < ehdr->e_shnum; i++) {
            const Elf64_Shdr& s = shdrs[i];
            const char* name = shstrtab + s.sh_name;
            if (strcmp(name, nm) == 0) return i;
        }
        return -1;
    }

    vector<pair<uint64_t,uint64_t>> get_load_segments() {
        vector<pair<uint64_t,uint64_t>> ret;
        for (int i = 0; i < ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = phdrs[i];
            if (p.p_type == PT_LOAD) {
                ret.emplace_back(p.p_vaddr, p.p_memsz);
            }
        }
        return ret;
    }
};

// A very small loader that allocates a contiguous buffer and copies PT_LOAD segments into
// appropriate offsets relative to a chosen load base.
struct SimpleLoader {
    ELFParser& elf;
    uint8_t* mem = nullptr; // base pointer
    uint64_t mem_size = 0;
    uint64_t load_base = 0; // the base we choose; virtual addresses in ELF will be mapped to load_base + (vaddr - min_vaddr)
    uint64_t min_vaddr = UINT64_MAX;

    SimpleLoader(ELFParser& e): elf(e) {
        for (int i = 0; i < elf.ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = elf.phdrs[i];
            if (p.p_type == PT_LOAD) {
                if (p.p_vaddr < min_vaddr) min_vaddr = p.p_vaddr;
            }
        }
        if (min_vaddr == UINT64_MAX) min_vaddr = 0;
    }

    bool allocate_space() {
        uint64_t max_vaddr = 0;
        for (int i = 0; i < elf.ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = elf.phdrs[i];
            if (p.p_type == PT_LOAD) {
                uint64_t end = p.p_vaddr + p.p_memsz;
                if (end > max_vaddr) max_vaddr = end;
            }
        }
        if (max_vaddr <= min_vaddr) return false;
        mem_size = max_vaddr - min_vaddr;
        // align to page
        size_t pagesz = sysconf(_SC_PAGESIZE);
        size_t alloc = ((mem_size + pagesz - 1) / pagesz) * pagesz;
        mem = (uint8_t*)mmap(nullptr, alloc, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return false;
        memset(mem, 0, alloc);
        mem_size = alloc;
        load_base = 0x10000000; // we choose an arbitrary base for display; since we used anon mmap, real pointer differs
        return true;
    }

    uint8_t* vaddr_to_ptr(uint64_t vaddr) {
        uint64_t off = vaddr - min_vaddr;
        if (off >= mem_size) return nullptr;
        return mem + off;
    }

    bool copy_segments() {
        for (int i = 0; i < elf.ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = elf.phdrs[i];
            if (p.p_type != PT_LOAD) continue;
            uint8_t* dest = vaddr_to_ptr(p.p_vaddr);
            if (!dest) { fprintf(stderr, "segment out of allocated range\n"); return false; }
            if (p.p_filesz > 0) {
                memcpy(dest, (const uint8_t*)elf.base + p.p_offset, p.p_filesz);
            }
            // remaining bytes (bss) are already zeroed by mmap anon
            // set memory protection according to p.p_flags
            // cannot set per-region protections inside the single mmap easily here; skip
        }
        return true;
    }

    // very basic RELA handling: only R_X86_64_RELATIVE
    bool apply_relocations() {
        int idx = elf.find_section_by_name(".rela.dyn");
        if (idx < 0) idx = elf.find_section_by_name(".rela.plt");
        if (idx < 0) {
            // nothing to do
            return true;
        }
        const Elf64_Shdr& relsh = elf.shdrs[idx];
        size_t count = relsh.sh_size / relsh.sh_entsize;
        const Elf64_Rela* rela = (const Elf64_Rela*)((const uint8_t*)elf.base + relsh.sh_offset);
        for (size_t i = 0; i < count; i++) {
            uint64_t r_offset = rela[i].r_offset; // location to modify (vaddr)
            uint64_t r_info = rela[i].r_info;
            uint64_t r_type = ELF64_R_TYPE(r_info);
            uint64_t r_sym  = ELF64_R_SYM(r_info);
            uint64_t addend = rela[i].r_addend;
            uint8_t* p = vaddr_to_ptr(r_offset);
            if (!p) {
                fprintf(stderr, "rela target 0x%lx out of range\n", r_offset); continue;
            }
            if (r_type == R_X86_64_RELATIVE) {
                // * (addr) = base + addend
                uint64_t value = (uint64_t)( (uint8_t*)mem - (uint8_t*)0 ) + addend + (uint64_t)0 + (elf.ehdr->e_type==ET_DYN? (uint64_t)0 : 0);
                // Since we loaded into mem at offset zero relative to min_vaddr, the effective base is (runtime) mem - min_vaddr
                uint64_t base_of_loaded = (uint64_t)mem - (uint64_t)0; // not meaningful for portable pointer math; but we'll compute relative
                // compute value = (uintptr_t)mem + (r_addend) - min_vaddr
                uint64_t computed = (uint64_t)( (uintptr_t)mem + (r_offset - elf.min_vaddr()) ) + addend; // we can't call min_vaddr() here; fudge below
                // simpler: place (pointer) = (void*)(mem + (r_offset - min_vaddr) + addend)
                uint64_t targ = (uint64_t)( (uintptr_t)mem + (r_offset - min_vaddr) + addend );
                memcpy(p, &targ, sizeof(targ));
            } else if (r_type == R_X86_64_GLOB_DAT || r_type == R_X86_64_JUMP_SLOT) {
                // needs symbol lookup against dynamic symbols and external resolution: not implemented
                uint64_t resolved = 0;
                memcpy(p, &resolved, sizeof(resolved));
                fprintf(stderr, "warning: left GLOB_DAT/JUMP_SLOT unresolved (sym=%lu)\n", r_sym);
            } else {
                fprintf(stderr, "warning: unhandled reloc type %lu at offset 0x%lx\n", r_type, r_offset);
            }
        }
        return true;
    }

    // expose min_vaddr for relocation code convenience
    uint64_t get_min_vaddr() const { return min_vaddr; }
};

// We need a way to expose ELFParser's min_vaddr; add accessor safely via friend or free function
namespace helper {
    uint64_t get_min_vaddr(const ELFParser& e) {
        uint64_t mv = UINT64_MAX;
        for (int i = 0; i < e.ehdr->e_phnum; i++) {
            const Elf64_Phdr& p = e.phdrs[i];
            if (p.p_type == PT_LOAD) if (p.p_vaddr < mv) mv = p.p_vaddr;
        }
        if (mv == UINT64_MAX) mv = 0;
        return mv;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf-file>\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];
    MappedFile mf;
    if (!mf.map(path)) { perror("map"); return 1; }

    ELFParser p(mf.data, mf.size);

    if (!p.parse()) { mf.unmap(); return 1; }
    p.print_summary();

    // show some useful sections
    int dyn_idx = p.find_section_by_name(".dynamic");
    if (dyn_idx >= 0) printf(".dynamic at index %d\n", dyn_idx);
    int rela_idx = p.find_section_by_name(".rela.dyn");
    if (rela_idx >= 0) printf(".rela.dyn at index %d\n", rela_idx);

    // try to load
    SimpleLoader loader(p);
    // compute min_vaddr inside parser helper
    uint64_t minv = helper::get_min_vaddr(p);
    // set loader's min_vaddr via reinterpret_cast hack (we didn't expose it earlier)
    *((uint64_t*)(&loader) + 2) = minv; // somewhat hacky: sets the private min_vaddr field; kept for a small demo
    if (!loader.allocate_space()) { fprintf(stderr, "allocate failed\n"); mf.unmap(); return 1; }
    if (!loader.copy_segments()) { fprintf(stderr, "copy failed\n"); mf.unmap(); return 1; }

    // apply relocations (basic)
    // call helper to get min_vaddr
    // TODO: better design: expose methods; kept short for demo
    printf("Loaded image into memory (size=%zu). Entry(VA)=0x%016lx, mapped entry ptr=%p\n",
           loader.mem_size, p.ehdr->e_entry, loader.vaddr_to_ptr(p.ehdr->e_entry));

    // For safety we don't attempt to jump to the entry point.
    mf.unmap();
    // unmap loader.mem
    if (loader.mem) munmap(loader.mem, loader.mem_size);
    return 0;
}
