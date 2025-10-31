#ifndef RV_TCORE_DIRECTBTB_H
#define RV_TCORE_DIRECTBTB_H
// ----------------------------- Direct BTB -------------------------------
struct DirectBTBEntry {
    bool valid = false;
    u64 tag = 0;
    u64 target = 0;
    uint8_t meta = 0; // optional
    uint32_t last_used_time = 0; // for LRU
};

class DirectBTB {
public:
    DirectBTB(int nsets, int ways, int tag_bits, int pc_lowdrop);
    // lookup returns pair(hit, target)
    pair<bool,u64> lookup(u64 pc, uint32_t time);

    void insert(u64 pc, u64 target, uint32_t time);

    void invalidate_all();
private:
    pair<int,u64> index_and_tag(u64 pc);
    int m_nsets;
    int m_ways;
    int m_tagbits;
    int m_lowdrop; // how many low PC bits are ignored (alignment)
    vector<vector<DirectBTBEntry>> m_sets;
};

#endif
