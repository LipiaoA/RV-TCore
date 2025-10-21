#include "model.h"

extern uint64_t g_cycles;

bool Model::build() {
    return true;
}

void  Model::reset() {
    return;
}

void Model::run() {
    while(true) {
        g_cycles++;
    }
    return;
}

bool Model::init(const std::string& path) {
    MappedFile elf;
    if (!elf.map(path.c_str)) {
        return false;
    }
    ELFReader reader(elf.m_fd, elf.m_size);
    reader.read(m_mem);
    return true;
}
