### 设计描述-实现



#### 1.流水线

设计一个支持多端口，多级流水线数据结构。该流水线包括阻塞，延迟保留功能，即包含每级流水线状态。正序Tick调用。不使用多重嵌套的vector，考虑性能情况，可以适当空间换时间。

```c++

// ------------------------ PipeSlot ------------------------
template<typename T>
struct PipeSlot {
    bool valid = false;
    T data{};
    bool stalled = false;
};

// ------------------------ MultiPortPipe with Stall (optimized) ------------------------
template<typename T>
class Pipe {
public:
    Pipe(size_t num_in, size_t num_out, size_t depth)
        : m_num_in(num_in), m_num_out(num_out), m_depth(depth)
    {
        assert(depth > 0);
        m_slots.resize(num_out * depth);      // 连续内存: stage*port
        m_input_buf.resize(num_in);
        m_ready_out.resize(num_out, true);
    }

    // ---------- 输入接口 ----------
    void write(size_t port, const T& data) {
        assert(port < m_num_in);
        m_input_buf[port] = data;
    }

    // ---------- 输出接口 ----------
    bool valid(size_t port) const {
        assert(port < m_num_out);
        return slot(m_depth - 1, port).valid;
    }

    const T& read(size_t port) const {
        assert(port < m_num_out);
        return slot(m_depth - 1, port).data;
    }

    void set_downstream_ready(size_t port, bool ready) {
        assert(port < m_num_out);
        m_ready_out[port] = ready;
    }

    // ---------- Tick / Update ----------
    void Update() {
        // 倒序推进流水段（从后向前）
        for (size_t d = m_depth - 1; d > 0; --d) {
            for (size_t p = 0; p < m_num_out; ++p) {
                PipeSlot<T>& dst = slot(d, p);
                PipeSlot<T>& src = slot(d - 1, p);
                if (!dst.stalled)
                    dst = src;
                else
                    dst.stalled = false; // 下周期再次尝试
            }
        }

        // 插入新的输入到 stage 0
        for (size_t p = 0; p < m_num_out && p < m_num_in; ++p) {
            PipeSlot<T>& s = slot(0, p);
            if (m_input_buf[p].has_value()) {
                s.data = *m_input_buf[p];
                s.valid = true;
                m_input_buf[p].reset();
            } else {
                s.valid = false;
            }
        }

        // 根据下游 ready/stall 决定是否暂停输出
        for (size_t p = 0; p < m_num_out; ++p) {
            PipeSlot<T>& s = slot(m_depth - 1, p);
            if (s.valid && !m_ready_out[p]) s.stalled = true;
        }
    }

    void Flush() {
        for (auto& s : m_slots) s.valid = false;
    }

    size_t depth() const { return m_depth; }
    size_t num_out() const { return m_num_out; }

private:
    size_t m_num_in;
    size_t m_num_out;
    size_t m_depth;

    std::vector<PipeSlot<T>> m_slots;       // stage*port 连续内存
    std::vector<std::optional<T>> m_input_buf;
    std::vector<bool> m_ready_out;

    // 索引访问 stage*port
    PipeSlot<T>& slot(size_t stage, size_t port) {
        return m_slots[stage * m_num_out + port];
    }
    const PipeSlot<T>& slot(size_t stage, size_t port) const {
        return m_slots[stage * m_num_out + port];
    }
};

// ------------------------ 测试结构 ------------------------
struct Instr { int id; std::string name; };

class FetchStage {
    int m_pc = 0;
public:
    void Tick(Pipe<Instr>& out) {
        for (size_t p = 0; p < out.num_out(); ++p) {
            Instr i{m_pc++, "instr"};
            out.write(p, i);
            std::cout << "[Fetch] issue instr " << i.id << " to port " << p << "\n";
        }
    }
};

class ExecuteStage {
public:
    void Tick(Pipe<Instr>& in) {
        for (size_t p = 0; p < in.num_out(); ++p) {
            if (in.valid(p)) {
                Instr i = in.read(p);
                std::cout << "[Execute] exec instr " << i.id << " from port " << p << "\n";
            }
        }
    }
};
```



#### 2.日志系统