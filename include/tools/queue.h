#ifndef RV_TCORE_QUEUES_H
#define RV_TCORE_QUEUES_H

#include "intf.h"

#include <vector>
#include <utility>
#include <deque>
#include <queue>
#include <cassert>
#include <iterator>
#include <functional>
#include <type_traits>

#define DEFAULT_QUEUE_CAPACITY 1024

template <typename T>
class ASyncQueue : public DevIntf {
public:
    explicit ASyncQueue(size_t capacity)
        : DevIntf("as_queue")
        , m_capacity(capacity)
        {
            stalled = false;
            m_write_buffer.clear();
            m_read_buffer.clear();
        }
    virtual ~ASyncQueue() = default;

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

    void flush_if(std::function<bool(const T&)> predicate)
    {
        for (size_t i = 0; i < m_write_buffer.size(); i++) {
            if (predicate(m_write_buffer[i])) {
                m_write_buffer.erase(m_write_buffer.begin() + i);
            }
        }
        for (size_t i = 0; i < m_read_buffer.size(); i++) {
            if (predicate(m_read_buffer[i])) {
                m_read_buffer.erase(m_read_buffer.begin() + i);
            }
        }
    }

    template <typename Fn>
    void for_each_mut(Fn && fn)
    {
        for_each_mut_impl(m_read_buffer, fn);
        for_each_mut_impl(m_write_buffer, fn);
    }

    template <typename Fn>
    void for_each_mut_read(Fn&& fn)
    {
        for_each_mut_impl(m_read_buffer, fn);
    }
    template <typename Fn>
    void for_each_mut_write(Fn&& fn)
    {
        for_each_mut_impl(m_write_buffer, fn);
    }
    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        for (const auto& item : m_read_buffer)
            fn(item);
        for (const auto& item : m_write_buffer)
            fn(item);
    }

    // 读端接口：从读缓冲区读取
    inline bool read(T &out)
    {
        if (m_read_buffer.empty())
            return false;
        out = std::move(m_read_buffer.front());
        m_read_buffer.pop_front();
        return true;
    }

    // 当前读队列是否为空
    inline bool empty() const
    {
        return m_read_buffer.empty();
    }

    inline bool full() const
    {
        return m_read_buffer.size() >= m_capacity;
    }

    // 在周期边界调用，将write缓冲区切换到read缓冲区
    void tick() override
    {
        if (stalled) {
            return;
        }
        for (size_t i = 0; i < m_write_buffer.size(); i++) {
            m_read_buffer.push_back(m_write_buffer[i]);
        }
        m_write_buffer.clear();
    }

    void build() override {}

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
    virtual void commit() override {
        if (stalled) {
            return;
        }
        for (size_t i = 0; i < m_write_buffer.size(); i++) {
            m_read_buffer.push_back(m_write_buffer[i]);
        }
        m_write_buffer.clear();
     }
private:
     template <typename Fn>
     static void for_each_mut_impl(std::deque<T>& buffer, Fn && fn)
     {
        for (size_t i = 0; i < buffer.size();) {
            if constexpr (std::is_same_v<
                std::invoke_result_t<Fn, T&>, bool>) {
                if (!fn(buffer[i])) {
                    buffer.erase(buffer.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            } else {
                fn(buffer[i]);
            }
            ++i;
        }
     }

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
    class const_iterator;

    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        inline iterator(RingQueue<T> *queue, size_t offset)
            : m_queue(queue)
            , m_offset(offset)
        {
        }

        inline reference operator*() const
        {
            const size_t index = (m_queue->m_head + m_offset) % m_queue->m_capacity;
            return m_queue->m_data[index];
        }

        inline pointer operator->() const
        {
            return &(**this);
        }

        inline iterator &operator++()
        {
            ++m_offset;
            return *this;
        }

        inline iterator operator++(int)
        {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        inline bool operator==(const iterator &other) const
        {
            return m_queue == other.m_queue && m_offset == other.m_offset;
        }

        inline bool operator!=(const iterator &other) const
        {
            return !(*this == other);
        }

    private:
        friend class const_iterator;
        RingQueue<T> *m_queue;
        size_t m_offset;
    };

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        inline const_iterator(const RingQueue<T> *queue, size_t offset)
            : m_queue(queue)
            , m_offset(offset)
        {
        }

        inline const_iterator(const iterator &it)
            : m_queue(it.m_queue)
            , m_offset(it.m_offset)
        {
        }

        inline reference operator*() const
        {
            const size_t index = (m_queue->m_head + m_offset) % m_queue->m_capacity;
            return m_queue->m_data[index];
        }

        inline pointer operator->() const
        {
            return &(**this);
        }

        inline const_iterator &operator++()
        {
            ++m_offset;
            return *this;
        }

        inline const_iterator operator++(int)
        {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        inline bool operator==(const const_iterator &other) const
        {
            return m_queue == other.m_queue && m_offset == other.m_offset;
        }

        inline bool operator!=(const const_iterator &other) const
        {
            return !(*this == other);
        }

    private:
        const RingQueue<T> *m_queue;
        size_t m_offset;
    };

    explicit RingQueue(size_t capacity = 0)
        : m_capacity(capacity + 1)  // 预留一格区分空/满
        , m_head(0)
        , m_tail(0)
    {
        m_data.resize(m_capacity);
    }

    ~RingQueue() {
        m_data.clear();
    }
    void resize(size_t capacity) {
        m_capacity = capacity + 1;
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

    inline iterator begin()
    {
        return iterator(this, 0);
    }

    inline iterator end()
    {
        return iterator(this, size());
    }

    inline const_iterator begin() const
    {
        return const_iterator(this, 0);
    }

    inline const_iterator end() const
    {
        return const_iterator(this, size());
    }

    inline const_iterator cbegin() const
    {
        return const_iterator(this, 0);
    }

    inline const_iterator cend() const
    {
        return const_iterator(this, size());
    }

private:
    size_t m_capacity;  // vector 实际长度（含哨位）
    size_t m_head;      // 队头下标
    size_t m_tail;      // 队尾下一位置下标
    std::vector<T> m_data;
};


template<typename T, size_t N>
class FixedArray : public std::array<T, N>
{
    using Base = FixedArray<T, N>;

public:
    using value_type = T;
    using size_type  = size_t;

    FixedArray() = default;
    ~FixedArray() = default;

    // 禁止拷贝（CPU模型里通常不希望拷贝大结构）
    FixedArray(const FixedArray&) = delete;
    FixedArray& operator=(const FixedArray&) = delete;

    // 允许移动
    FixedArray(FixedArray&&) = default;
    FixedArray& operator=(FixedArray&&) = default;

public:
    // ===== 容量 =====
    constexpr size_type capacity() const noexcept { return N; }
    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full()  const noexcept { return size_ == N; }

    // ===== 清空 =====
    void clear() noexcept
    {
        // 对非trivial类型调用析构
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < size_; i++)
                (*this)[i].~T();
        }
        size_ = 0;
    }

    // ===== push_back =====
    void push_back(const T& value)
    {
        assert(size_ < N);
        (*this)[size_] = value;
        size_++;
    }

    void push_back(T&& value)
    {
        assert(size_ < N);
        (*this)[size_] = std::move(value);
        size_++;
    }

    // ===== emplace_back =====
    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        assert(size_ < N);
        T* ptr = &(*this)[size_];
        new (ptr) T(std::forward<Args>(args)...);
        return (*this)[size_++];
    }

    // ===== pop_back =====
    void pop_back()
    {
        assert(size_ > 0);
        size_--;
        if constexpr (!std::is_trivially_destructible_v<T>)
            (*this)[size_].~T();
    }

    // ===== resize =====
    void resize(size_t newSize)
    {
        assert(newSize <= N);

        if (newSize < size_) {
            // 缩小 → 析构
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = newSize; i < size_; i++)
                    (*this)[i].~T();
            }
        }
        else {
            // 扩大 → 默认构造
            for (size_t i = size_; i < newSize; i++)
                new (&(*this)[i]) T();
        }

        size_ = newSize;
    }

    // ===== 只遍历有效元素 =====
    T* begin_valid() { return this->data(); }
    T* end_valid()   { return this->data() + size_; }

    const T* begin_valid() const { return this->data(); }
    const T* end_valid()   const { return this->data() + size_; }

private:
    size_t size_ = 0;
};

#endif // RV_TCORE_QUEUES_H
