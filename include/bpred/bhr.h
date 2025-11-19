#ifndef TCORE_BHR_H
#define TCORE_BHR_H

class BHR {
public:
    BHR(int bits): m_bits(bits), m_val(0) {}
    void        push(bool taken) { m_val = ((m_val<<1) | (taken?1:0)) & ((1ull<<m_bits)-1); }
    u64         value() const { return m_val; }
    void        set(u64 v) { m_val = v & ((1ull<<m_bits)-1); }
private:
    int         m_bits;
    u64         m_val;
};

#endif
