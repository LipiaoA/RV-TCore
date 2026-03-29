#include <iostream>

#include "model.h"


extern u64 g_cycles;
extern u64 g_max_cycles;

bool Model::build() {
    m_core = new Top("core");
    m_core->build();
    m_log = &LogSystem::instance();
    return true;
}

void  Model::reset() {
    return;
}

void Model::run() {
    u16 op = 0;
    u64 addr = m_entry;

    m_core->reset();

    while(g_cycles < g_max_cycles) {
        g_cycles++;
        op = m_mem->load16(addr);
        m_log->write("\nCycle:%d \n-------------------------", g_cycles);
        LOG_LEVEL1("Total INFO", " addr:0x%lx, op:%lx", addr, op);
        addr += 2;
        m_core->tick();
        m_core->commit();

        if (m_core->exit()) {
            break;
        }
    }
    return;
}

bool Model::init(const std::string& path) {
    MappedFile elf;
    if (!elf.map(path.c_str())) {
        return false;
    }
    ELFReader reader(elf.data, elf.size);
    m_mem = new Memory();
    reader.read(*m_mem);
    m_entry = reader.get_entry();
    return true;
}


Model::~Model() {
    delete_ptr(m_mem);
    delete_ptr(m_core);
}