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
    m_core_config.fetch_group_size = 32;
    m_core_config.fetch_bank_size = 16;
    m_core_config.decode_width = 4;
    m_core_config.rename_width = 4;
    m_core_config.commit_width = 4;
    m_core_config.exec_commit_width = 4;

    m_core_config.prf_width = 4;
    m_core_config.prf_capacity = 192;

    // queues / structures
    m_core_config.rob_entries = 128;
    m_core_config.iq_entries  = 64;
    m_core_config.lq_entries  = 32;
    m_core_config.sq_entries  = 32;

    // LSU
    m_core_config.load_q_size       = 128;
    m_core_config.store_q_size      = 128;
    m_core_config.agu_num          = 4;
    m_core_config.mshr_buffer_size  = 128;
    m_core_config.lsu_issue_width   = 2;

    m_core_config.alu_stages        = 2;
    m_core_config.fpu_stages        = 4;
    m_core_config.bru_stages        = 2;

    // issue / executetop
    m_core_config.int_issue_width = 4;
    m_core_config.ls_issue_width  = 2;
    m_core_config.fp_issue_width  = 2;
    m_core_config.alu_num         = 6;
    m_core_config.fpu_num         = 2;
    m_core_config.bru_num         = 1;

    // i-TLB configuration
    m_core_config.itlb_hit_latency        = 12;
    m_core_config.itlb_miss_penalty       = 3;
    m_core_config.itlb_install_latency    = 1;

    // d-TLB configuration
    m_core_config.dtlb_hit_latency        = 12;
    m_core_config.dtlb_miss_penalty       = 3;
    m_core_config.dtlb_install_latency    = 1;

    // d-cache configuration
    m_core_config.dcache_size            = 64 * 1024;
    m_core_config.dcache_line_size       = 64;
    m_core_config.dcache_ways            = 4;
    m_core_config.dcache_fetch_width     = 4;
    m_core_config.dcache_hit_latency     = 2;
    m_core_config.dcache_miss_penalty    = 8;
    m_core_config.dcache_install_latency = 2;
    m_core_config.dcache_wb_latency      = 5;
    m_core_config.dcache_inclusive       = 1;

    // i-cache configuration
    m_core_config.icache_size             = 32 * 1024;
    m_core_config.icache_line_size        = 64;
    m_core_config.icache_ways             = 4;
    m_core_config.icache_fetch_width      = 4;
    m_core_config.icache_hit_latency      = 1;
    m_core_config.icache_miss_penalty     = 3;
    m_core_config.icache_install_latency  = 1;
    m_core_config.icache_writeback_latency = 8;
    m_core_config.icache_inclusive        = 1;
    m_core_config.icache_group_size       = 32;
    m_core_config.icache_bank_size        = 16;
    m_core_config.icache_bank_count       = 4;
    m_core_config.icache_group_count      = 2;

    // L2 cache configuration
    m_core_config.l2_size             = 512 * 1024;
    m_core_config.l2_ways             = 8;
    m_core_config.l2_line_size        = 64;
    m_core_config.l2_fetch_width      = 4;
    m_core_config.l2_hit_latency      = 5;
    m_core_config.l2_miss_penalty     = 8;
    m_core_config.l2_install_latency  = 5;
    m_core_config.l2_writeback_latency = 8;
    m_core_config.l2_inclusive        = 1;

    // legacy L1 size fields track I$/D$ capacity
    m_core_config.l1iSize = static_cast<int>(m_core_config.icache_size);
    m_core_config.l1dSize = static_cast<int>(m_core_config.dcache_size);

    m_core_config.bim_entries = 0x4000;
    m_core_config.bim_hist_len = 16;
    m_core_config.bim_fetch_width = 4;
    m_core_config.btb_nsets = 256;
    m_core_config.btb_ways = 4;
    m_core_config.btb_tag_bits = 16;
    m_core_config.btb_pc_lowdrop = 2;

    m_core_config.ras_capacity = 64;
    m_core_config.ras_tag_bits = 10;

    m_core_config.flush_width = 4;
    return;
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

TageConfig TCoreConfig::tage_config() const {
    TageConfig conf;
    /* TODO: implement */
    return conf;
}

BIMConfig TCoreConfig::bim_config() const {
    BIMConfig conf;
    conf.entries = m_core_config.bim_entries;
    conf.hist_len = m_core_config.bim_hist_len;
    return conf;
}

BTBConfig TCoreConfig::btb_config() const {
    BTBConfig conf;
    conf.nsets = m_core_config.btb_nsets;
    conf.ways = m_core_config.btb_ways;
    conf.tag_bits = m_core_config.btb_tag_bits;
    conf.pc_lowdrop = m_core_config.btb_pc_lowdrop;
    return conf;
}


BPConfig TCoreConfig::bp_config() const {
    BPConfig conf;
    conf.tage_config = tage_config();
    conf.bim_config = bim_config();
    conf.btb_config = btb_config();
    conf.ras_capacity = m_core_config.ras_capacity;
    return conf;
}

LSUConfig TCoreConfig::lsu_config() const {
    LSUConfig conf;
    conf.load_q_size            = m_core_config.load_q_size;
    conf.store_q_size           = m_core_config.store_q_size;
    conf.agu_num               = m_core_config.agu_num;
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
    conf.prf_capacity = m_core_config.prf_capacity;
    return conf;
}

RegFileConfig TCoreConfig::regfile_config() const {
    RegFileConfig conf;
    conf.width = m_core_config.prf_width;
    conf.capacity = m_core_config.prf_capacity;
    return conf;
}

FlushUnitConfig TCoreConfig::flush_config() const {
    FlushUnitConfig conf;
    conf.width = m_core_config.flush_width;
    return conf;
}

FetchConfig TCoreConfig::fetch_config() const {
    FetchConfig conf;
    conf.fetch_width = m_core_config.fetch_width;
    conf.group_size = m_core_config.fetch_group_size;
    conf.bank_size = m_core_config.fetch_bank_size;
    conf.cache_line_size = m_core_config.icache_line_size;
    conf.bank_count = m_core_config.icache_bank_count;
    conf.group_count = m_core_config.icache_group_count;
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
    conf.agu_num = m_core_config.agu_num;
    conf.commit_width = m_core_config.exec_commit_width;
    ALUConfig alu_conf;
    FPUConfig fpu_conf;
    BRUConfig bru_conf;
    LSUConfig lsu_conf;
    alu_conf.stages = m_core_config.alu_stages;
    fpu_conf.stages = m_core_config.fpu_stages;
    bru_conf.stages = m_core_config.bru_stages;
    lsu_conf.issue_width = m_core_config.lsu_issue_width;
    conf.alu_conf = alu_conf;
    conf.fpu_conf = fpu_conf;
    conf.bru_conf = bru_conf;
    conf.lsu_conf = lsu_conf;
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

RASConfig TCoreConfig::ras_config() const {
    RASConfig conf;
    conf.capacity = m_core_config.ras_capacity;
    conf.tag_bits = m_core_config.ras_tag_bits;
    return conf;
}