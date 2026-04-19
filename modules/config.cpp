#include "config.h"

#include <cstdio>

TCoreConfig::TCoreConfig() {
    init_config();
}

TCoreConfig::TCoreConfig(const CoreConfig& core) : m_core_config(core) {}

TCoreConfig::~TCoreConfig() {
    print_config();
}

TCoreConfig& TCoreConfig::get_instance() {
    static TCoreConfig instance;
    return instance;
}

void TCoreConfig::init_config() {
    // pipeline
    m_core_config.fetch_width  = 4;
    m_core_config.decode_width = 4;
    m_core_config.rename_width = 4;
    m_core_config.commit_width = 4;

    // queues / structures
    m_core_config.rob_entries = 128;
    m_core_config.iq_entries  = 64;
    m_core_config.lq_entries  = 32;
    m_core_config.sq_entries  = 32;

    // LSU
    m_core_config.load_q_size       = 128;
    m_core_config.store_q_size      = 128;
    m_core_config.agus_num          = 4;
    m_core_config.mshr_buffer_size  = 128;
    m_core_config.lsu_issue_width   = 2;

    // issue / executetop
    m_core_config.int_issue_width = 4;
    m_core_config.ls_issue_width  = 2;
    m_core_config.fp_issue_width  = 2;
    m_core_config.alu_num         = 6;
    m_core_config.fpu_num         = 2;
    m_core_config.bru_num         = 1;

    // i-cache configuration
    m_core_config.icache_size             = 32 * 1024;
    m_core_config.icache_line_size        = 64;
    m_core_config.icache_ways             = 4;
    m_core_config.icache_fetch_width      = 4;
    m_core_config.icache_hit_latency      = 1;
    m_core_config.icache_miss_penalty     = 3;
    m_core_config.icache_install_latency  = 1;
    m_core_config.icache_writeback_latency = 8;

    // d-cache configuration
    m_core_config.dcache_size            = 64 * 1024;
    m_core_config.dcache_line_size       = 64;
    m_core_config.dcache_ways            = 4;
    m_core_config.dcache_fetch_width     = 4;
    m_core_config.dcache_hit_latency     = 2;
    m_core_config.dcache_miss_penalty    = 8;
    m_core_config.dcache_install_latency = 2;
    m_core_config.dcache_wb_latency      = 5;
    m_core_config.dcache_inclusive       = false;

    // L2 cache configuration
    m_core_config.l2_size             = 512 * 1024;
    m_core_config.l2_ways             = 8;
    m_core_config.l2_line_size        = 64;
    m_core_config.l2_fetch_width      = 4;
    m_core_config.l2_hit_latency      = 10;
    m_core_config.l2_miss_penalty     = 30;
    m_core_config.l2_install_latency  = 5;
    m_core_config.l2_writeback_latency = 12;
    m_core_config.l2_inclusive        = true;

    // legacy L1 size fields track I$/D$ capacity
    m_core_config.l1iSize = static_cast<int>(m_core_config.icache_size);
    m_core_config.l1dSize = static_cast<int>(m_core_config.dcache_size);
}

void TCoreConfig::print_config() const {
    std::printf("[TCoreConfig] rob=%d iq=%d icache=%u dcache=%u l2=%u\n",
                m_core_config.rob_entries,
                m_core_config.iq_entries,
                m_core_config.icache_size,
                m_core_config.dcache_size,
                m_core_config.l2_size);
}

DecodeConfig TCoreConfig::decode_config() const {
    DecodeConfig conf;
    conf.width = static_cast<u32>(m_core_config.decode_width);
    return conf;
}

LSUConfig TCoreConfig::lsu_config() const {
    LSUConfig conf;
    conf.load_q_size            = m_core_config.load_q_size;
    conf.store_q_size           = m_core_config.store_q_size;
    conf.agus_num               = m_core_config.agus_num;
    conf.mshr_buffer_size       = m_core_config.mshr_buffer_size;
    conf.issue_width            = m_core_config.lsu_issue_width;
    return conf;
}

IssueConfig TCoreConfig::issue_config() const {
    IssueConfig conf;
    conf.int_issue_width = m_core_config.int_issue_width;
    conf.ls_issue_width  = m_core_config.ls_issue_width;
    conf.fp_issue_width  = m_core_config.fp_issue_width;
    return conf;
}

ROBConfig TCoreConfig::rob_config() const {
    ROBConfig conf;
    conf.capacity = static_cast<u32>(m_core_config.rob_entries);
    return conf;
}

RenameConfig TCoreConfig::rename_config() const {
    RenameConfig conf;
    conf.width = m_core_config.rename_width;
    return conf;
}

FetchConfig TCoreConfig::fetch_config() const {
    FetchConfig conf;
    conf.fetch_width = static_cast<u32>(m_core_config.fetch_width);
    return conf;
}

CommitConfig TCoreConfig::commit_config() const {
    CommitConfig conf;
    conf.width = static_cast<u32>(m_core_config.commit_width);
    return conf;
}

ExecConfig TCoreConfig::execute_config() const {
    ExecConfig conf;
    conf.alu_num = m_core_config.alu_num;
    conf.fpu_num = m_core_config.fpu_num;
    conf.bru_num = m_core_config.bru_num;
    return conf;
}

CacheConfig TCoreConfig::l2_config() const {
    CacheConfig conf;
    conf.size             = m_core_config.l2_size;
    conf.ways             = m_core_config.l2_ways;
    conf.line_size        = m_core_config.l2_line_size;
    conf.fetch_width      = m_core_config.l2_fetch_width;
    conf.hit_latency      = m_core_config.l2_hit_latency;
    conf.miss_penalty     = m_core_config.l2_miss_penalty;
    conf.install_latency  = m_core_config.l2_install_latency;
    conf.writeback_latency = m_core_config.l2_writeback_latency;
    conf.inclusion_policy   = static_cast<InclusionPolicy>(m_core_config.l2_inclusive);
    return conf;
}

CacheConfig TCoreConfig::icache_config() const {
    CacheConfig conf;
    conf.size               = m_core_config.icache_size;
    conf.line_size          = m_core_config.icache_line_size;
    conf.ways               = m_core_config.icache_ways;
    conf.fetch_width        = m_core_config.icache_fetch_width;
    conf.hit_latency        = m_core_config.icache_hit_latency;
    conf.miss_penalty       = m_core_config.icache_miss_penalty;
    conf.install_latency    = m_core_config.icache_install_latency;
    conf.writeback_latency  = m_core_config.icache_writeback_latency;
    conf.inclusion_policy   = static_cast<InclusionPolicy>(m_core_config.icache_inclusive);
    return conf;
}

CacheConfig TCoreConfig::dcache_config() const {
    CacheConfig conf;
    conf.size               = m_core_config.dcache_size;
    conf.line_size          = m_core_config.dcache_line_size;
    conf.ways               = m_core_config.dcache_ways;
    conf.fetch_width        = m_core_config.dcache_fetch_width;
    conf.hit_latency        = m_core_config.dcache_hit_latency;
    conf.miss_penalty       = m_core_config.dcache_miss_penalty;
    conf.install_latency    = m_core_config.dcache_install_latency;
    conf.writeback_latency  = m_core_config.dcache_wb_latency;
    conf.inclusion_policy   = static_cast<InclusionPolicy>(m_core_config.dcache_inclusive);
    return conf;
}
