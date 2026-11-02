#include <iostream>

#include "model.h"
#include "verify/ref_model_checker.h"


extern u64 g_cycles;
extern u64 g_max_cycles;

bool Model::build() {
    m_top = new Top(TCoreConfig::get_instance());
    m_top->build();
    m_mem = m_top->get_mem();
    m_topdown = m_top->get_topdown();
    m_mem->add_segment(STACK_POINTER - STACK_SIZE, STACK_SIZE);
    m_mem->store64(STACK_POINTER - 8, 0x1);

    CoreDeps deps = m_top->get_core_deps();
    m_core = new Core(deps);
    m_core->build();
    m_core->reset();

    m_ref = new FuncModel();
    VeriRef::instance().bind(m_ref);
    VeriRef::instance().enable(true);

    m_log = &LogSystem::instance();
    m_log->set_level(LogLevel::LEVEL3);
    // m_log->add_writer(std::make_shared<ConsoleSink>());
    m_log->add_writer(std::make_shared<FileSink>("system.log"));
    g_max_cycles = 5000;
    return true;
}

void  Model::reset() {
    return;
}

void Model::run() {
    while(g_cycles < g_max_cycles) {
        g_cycles++;
        m_log->write("\nCycle:%d \n-------------------------", g_cycles);
        m_core->tick();
        m_core->commit();
        if (m_core->exit()) {
            break;
        }
    }
    m_topdown->analyze();
    m_topdown->report();
    return;
}

PerfStats* Model::perf_stats()
{
    return m_top->perf_stats();
}

bool Model::init(const std::string& path) {
    MappedFile elf;
    if (!elf.map(path.c_str())) {
        assert(false);
    }
    ELFReader reader(elf.data, elf.size);
    reader.read(*m_mem);
    m_entry = reader.get_entry();
    m_core->init(m_entry);

    if (m_ref && !m_ref->init(path)) {
        std::cerr << "failed to init reference func model" << std::endl;
        return false;
    }
    VeriRef::instance().reset();
    return true;
}


Model::~Model() {
    delete_ptr(m_ref);
    delete_ptr(m_core);
    delete_ptr(m_top);
}
