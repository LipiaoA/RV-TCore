#ifndef RV_TCORE_RAS_H
#define RV_TCORE_RAS_H

// ----------------------------- RAS --------------------------
class RAS {
public:
    RAS(int depth): m_depth(depth) { m_stack.reserve(depth); }
    void speculative_push(u64 ret_addr) { m_spec_push.push_back(ret_addr); }
    void speculative_pop() { if (!m_spec_pop_done) { m_spec_pop_done=true; m_spec_popped_addr = peek_spec_top(); } }

    u64 peek_spec_top() const {
        // compute top considering speculative pushes/pops
        int size = (int)m_stack.size() + (int)m_spec_push.size();
        if (m_spec_pop_done) --size;
        if (size <= 0) return 0; // invalid
        if (!m_spec_push.empty()) return m_spec_push.back();
        return m_stack.back();
    }

    void commit_push() {
        for (u64 a : m_spec_push) {
            if ((int)m_stack.size() >= m_depth) {
                // overflow -> drop oldest
                m_stack.erase(m_stack.begin());
            }
            m_stack.push_back(a);
        }
        m_spec_push.clear();
        m_spec_pop_done = false;
    }
    void commit_pop() {
        if (m_spec_pop_done) {
            if (!m_stack.empty()) m_stack.pop_back();
            m_spec_pop_done = false;
        }
    }

    void clear_speculation() { m_spec_push.clear(); m_spec_pop_done=false; }

    bool empty() const { return m_stack.empty() && m_spec_push.empty(); }

private:
    int m_depth;
    std::vector<u64>        m_stack;
    std::vector<u64>        m_spec_push; // speculative pushes
    bool                    m_spec_pop_done = false;
    u64                     m_spec_popped_addr = 0;
};

#endif
