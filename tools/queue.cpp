
#include <deque>


#ifndef ASYNC_QUEUE_H
#define ASYNC_QUEUE_H

#include <vector>
#include <utility>
#include <cassert>

template <typename T>
class AsyncQueue {
public:
    explicit AsyncQueue(size_t capacity)
        : m_capacity(capacity)
    {
        m_read_buffer.reserve(capacity);
        m_write_buffer.reserve(capacity);
    }

    // 写端接口：向写缓冲区写入数据
    inline bool write(const T &item)
    {
        if (m_write_buffer.size() >= m_capacity)
            return false;
        m_write_buffer.push_back(item);
        return true;
    }

    inline bool write(T &&item)
    {
        if (m_write_buffer.size() >= m_capacity)
            return false;
        m_write_buffer.push_back(std::move(item));
        return true;
    }

    // 读端接口：从读缓冲区读取
    inline bool read(T &out)
    {
        if (m_read_index >= m_read_buffer.size())
            return false;
        out = m_read_buffer[m_read_index++];
        return true;
    }

    // 当前读队列是否为空
    inline bool empty() const
    {
        return m_read_index >= m_read_buffer.size();
    }

    // 当前写队列是否满
    inline bool full() const
    {
        return m_write_buffer.size() >= m_capacity;
    }

    // 在周期边界调用，将write缓冲区切换到read缓冲区
    inline void tick()
    {
        // 交换两个缓冲区，避免拷贝
        m_read_buffer.swap(m_write_buffer);
        m_write_buffer.clear();
        m_read_index = 0;
    }

    // 获取当前读缓冲区可用元素数量
    inline size_t size() const
    {
        return m_read_buffer.size() - m_read_index;
    }

    inline void clear()
    {
        m_read_buffer.clear();
        m_write_buffer.clear();
        m_read_index = 0;
    }

private:
    size_t m_capacity;
    std::vector<T> m_read_buffer;
    std::vector<T> m_write_buffer;
    size_t m_read_index = 0;
};

#endif // ASYNC_QUEUE_H
