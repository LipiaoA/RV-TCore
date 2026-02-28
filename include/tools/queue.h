#ifndef RV_TCORE_QUEUES_H
#define RV_TCORE_QUEUES_H

#include <vector>
#include <utility>
#include <deque>
#include <queue>
#include <cassert>

#define DEFAULT_QUEUE_CAPACITY 1024

template <typename T>
class ASyncQueue : public DevIntf {
public:
    explicit ASyncQueue(size_t capacity)
        : DevIntf()
        , m_capacity(capacity)
        {
            stalled = false;
        }
    virtual ~ASyncQueue() = default;

    // 写端接口：向写缓冲区写入数据
    inline bool write(const T &item)
    {
        return m_write_buffer.push_back(item);
    }

    inline bool write(T &&item)
    {
        return m_write_buffer.push_back(std::move(item));
    }

    // 读端接口：从读缓冲区读取
    inline bool read(T &out)
    {
        return m_read_buffer.pop_front(out);
    }

    // 当前读队列是否为空
    inline bool empty() const
    {
        return m_read_buffer.empty();
    }

    // 在周期边界调用，将write缓冲区切换到read缓冲区
    inline void tick()
    {
        if (stalled) {
            return;
        }
        for (size_t i = 0; i < m_write_buffer.size(); i++) {
            m_read_buffer.push_back(m_write_buffer[i]);
        }
        m_write_buffer.clear();
    }

    // 获取当前读缓冲区可用元素数量
    inline size_t size() const
    {
        return m_read_buffer.size();
    }

    inline void stall()
    {
        stalled = true;
    }

    inline void unstall()
    {
        stalled = false;
    }

    inline void clear()
    {
        m_read_buffer.clear();
        m_write_buffer.clear();
    }
    virtual void commit() override { return; }

private:
    bool                    stalled;
    size_t                  m_capacity;
    std::deque<T>           m_read_buffer;
    std::deque<T>           m_write_buffer;
};


template <typename T>
class SyncQueue
{
public:
    explicit SyncQueue(size_t capacity)
        : m_capacity(capacity)
    {
        m_queue.reserve(capacity);
    }

    // 写端接口：向写缓冲区写入数据
    inline bool write(const T &item)
    {
        if (m_queue.size() >= m_capacity)
            return false;
        m_queue.push(item);
        return true;
    }

    // 读端接口：从读缓冲区读取
    inline bool read(T &out)
    {
        if (m_queue.empty())
            return false;
        out = m_queue.front();
        m_queue.pop();
        return true;
    }

    // 当前读队列是否为空
    inline bool empty() const
    {
        return m_queue.empty();
    }

    // 当前写队列是否满
    inline bool full() const
    {
        return m_queue.size() >= m_capacity;
    }

    // 获取当前读缓冲区可用元素数量
    inline size_t size() const
    {
        return m_queue.size();
    }

    inline void clear()
    {
        m_queue.clear();
    }

private:
    size_t m_capacity;
    std::queue<T> m_queue;
};

// 基于 vector 的循环队列模板
template <typename T>
class RingQueue {
public:
    explicit RingQueue(size_t capacity)
        : m_capacity(capacity + 1)  // 预留一格区分空/满
        , m_head(0)
        , m_tail(0)
    {
        m_data.resize(m_capacity);
    }

    inline bool write(const T &item)
    {
        if (full())
            return false;
        m_data[m_tail] = item;
        m_tail = (m_tail + 1) % m_capacity;
        return true;
    }

    inline bool write(T &&item)
    {
        if (full())
            return false;
        m_data[m_tail] = std::move(item);
        m_tail = (m_tail + 1) % m_capacity;
        return true;
    }

    inline bool pop(T &out)
    {
        if (empty())
            return false;
        out = std::move(m_data[m_head]);
        m_head = (m_head + 1) % m_capacity;
        return true;
    }

    inline bool empty() const
    {
        return m_head == m_tail;
    }

    inline bool full() const
    {
        return (m_tail + 1) % m_capacity == m_head;
    }

    inline size_t size() const
    {
        return (m_tail + m_capacity - m_head) % m_capacity;
    }

    inline size_t capacity() const
    {
        return m_capacity - 1;  // 实际可存元素数
    }

    inline void clear()
    {
        m_head = 0;
        m_tail = 0;
    }

    // 访问队首元素（不弹出）
    inline T &front()
    {
        assert(!empty());
        return m_data[m_head];
    }

    inline const T &front() const
    {
        assert(!empty());
        return m_data[m_head];
    }

private:
    size_t m_capacity;  // vector 实际长度（含哨位）
    size_t m_head;      // 队头下标
    size_t m_tail;      // 队尾下一位置下标
    std::vector<T> m_data;
};

#endif // RV_TCORE_QUEUES_H
