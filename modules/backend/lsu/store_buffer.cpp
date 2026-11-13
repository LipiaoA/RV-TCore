#include "store_buffer.h"

#include "log.h"

namespace {

bool dword_match(uint64_t lhs, uint64_t rhs)
{
    return (lhs >> 3) == (rhs >> 3);
}

} // namespace

StoreBuffer::StoreBuffer(size_t capacity)
{
    set_capacity(capacity);
}

u8 StoreBuffer::default_byte_mask(uint64_t addr, u32 width)
{
    const u32 shift = static_cast<u32>(addr & 7ULL);
    switch (width) {
    case 1:
        return static_cast<u8>(1u << shift);
    case 2:
        return static_cast<u8>(3u << (shift & ~1u));
    case 4:
        return static_cast<u8>((shift >= 4) ? 0xF0u : 0x0Fu);
    case 8:
    default:
        return 0xFFu;
    }
}

void StoreBuffer::set_capacity(size_t capacity)
{
    if (capacity == 0) {
        capacity = 16;
    }
    if (capacity > MAX_STORE_BUFFER_SIZE) {
        capacity = MAX_STORE_BUFFER_SIZE;
    }
    m_capacity = capacity;
    clear();
}

void StoreBuffer::push(const SBEntry& entry)
{
    if (is_full() || m_capacity == 0) {
        return;
    }
    SBEntry slot = entry;
    slot.valid = true;
    slot.writing = false;
    if (slot.width == 0) {
        slot.width = 8;
    }
    if (slot.byte_mask == 0) {
        const uint64_t mask_addr = slot.paddr ? slot.paddr : slot.vaddr;
        slot.byte_mask = default_byte_mask(mask_addr, slot.width);
    }
    m_entries[m_tail] = slot;
    m_tail = (m_tail + 1) % m_capacity;
    m_size++;
}

void StoreBuffer::pop()
{
    if (is_empty()) {
        return;
    }
    m_entries[m_head] = SBEntry();
    m_head = (m_head + 1) % m_capacity;
    m_size--;
}

bool StoreBuffer::is_full() const
{
    return m_capacity == 0 || m_size >= m_capacity;
}

bool StoreBuffer::is_empty() const
{
    return m_size == 0;
}

SBEntry& StoreBuffer::front()
{
    return m_entries[m_head];
}

const SBEntry& StoreBuffer::front() const
{
    return m_entries[m_head];
}

SBEntry& StoreBuffer::back()
{
    if (is_empty() || m_capacity == 0) {
        return m_entries[m_head];
    }
    return m_entries[(m_tail + m_capacity - 1) % m_capacity];
}

const SBEntry& StoreBuffer::back() const
{
    if (is_empty() || m_capacity == 0) {
        return m_entries[m_head];
    }
    return m_entries[(m_tail + m_capacity - 1) % m_capacity];
}

size_t StoreBuffer::size() const
{
    return m_size;
}

size_t StoreBuffer::head() const
{
    return m_head;
}

size_t StoreBuffer::tail() const
{
    return m_tail;
}

size_t StoreBuffer::capacity() const
{
    return m_capacity;
}

void StoreBuffer::clear()
{
    for (auto& entry : m_entries) {
        entry = SBEntry {};
    }
    m_head = 0;
    m_tail = 0;
    m_size = 0;
}

void StoreBuffer::print() const
{
    LOG_LEVEL1("StoreBuffer", "StoreBuffer size:%zu/%zu head:%zu tail:%zu",
        m_size, m_capacity, m_head, m_tail);
}

SBForward StoreBuffer::find_youngest(uint64_t addr, u8 load_mask) const
{
    SBForward result {};
    if (m_size == 0 || m_capacity == 0 || load_mask == 0) {
        return result;
    }
    for (size_t n = 0; n < m_size; ++n) {
        const size_t logical = m_size - 1 - n;
        const SBEntry& entry = m_entries[(m_head + logical) % m_capacity];
        if (!entry.valid) {
            continue;
        }
        const bool va_match = dword_match(entry.vaddr, addr);
        const bool pa_match = dword_match(entry.paddr, addr);
        if (!va_match && !pa_match) {
            continue;
        }
        if ((entry.byte_mask & load_mask) == 0) {
            continue;
        }
        result.hit = true;
        result.full_cover = (entry.byte_mask & load_mask) == load_mask;
        result.addr = va_match ? entry.vaddr : entry.paddr;
        result.data = entry.data;
        result.width = entry.width ? entry.width : 8;
        result.store_id = entry.store_id;
        result.byte_mask = entry.byte_mask;
        return result;
    }
    return result;
}
