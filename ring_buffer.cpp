//
// Created by pengx on 2026/2/10.
//

#include "ring_buffer.hpp"

#include <algorithm>

RingBuffer::RingBuffer(const size_t capacity) : _buffer(capacity), _capacity(capacity) {}

size_t RingBuffer::write(const uint8_t* data, const size_t len) {
    if (!data || len == 0)
        return 0;

    const size_t write_pos = _write_pos.load(std::memory_order_relaxed);
    const size_t read_pos = _read_pos.load(std::memory_order_acquire);
    const size_t writable = (read_pos > write_pos) ? (read_pos - write_pos - 1) : (_capacity - write_pos + read_pos);

    const size_t to_write = std::min(len, writable);
    if (to_write == 0)
        return 0;

    // 第一段：从write_pos到末尾
    const size_t first_part = std::min(to_write, _capacity - write_pos);
    std::memcpy(_buffer.data() + write_pos, data, first_part);

    // 第二段：从开头
    if (first_part < to_write) {
        std::memcpy(_buffer.data(), data + first_part, to_write - first_part);
    }

    _write_pos.store((write_pos + to_write) % _capacity, std::memory_order_release);
    return to_write;
}

size_t RingBuffer::read(uint8_t* data, const size_t len) {
    if (!data || len == 0)
        return 0;

    const size_t read_pos = _read_pos.load(std::memory_order_relaxed);
    const size_t write_pos = _write_pos.load(std::memory_order_acquire);
    const size_t readable = (write_pos >= read_pos) ? (write_pos - read_pos) : (_capacity - read_pos + write_pos);

    const size_t to_read = std::min(len, readable);
    if (to_read == 0)
        return 0;

    // 第一段：从read_pos到末尾
    const size_t first_part = std::min(to_read, _capacity - read_pos);
    std::memcpy(data, _buffer.data() + read_pos, first_part);

    // 第二段：从开头
    if (first_part < to_read) {
        std::memcpy(data + first_part, _buffer.data(), to_read - first_part);
    }

    _read_pos.store((read_pos + to_read) % _capacity, std::memory_order_release);
    return to_read;
}

size_t RingBuffer::discard(const size_t len) {
    if (len == 0)
        return 0;
    const size_t read_pos = _read_pos.load(std::memory_order_relaxed);
    const size_t write_pos = _write_pos.load(std::memory_order_acquire);
    const size_t readable = (write_pos >= read_pos) ? (write_pos - read_pos) : (_capacity - read_pos + write_pos);

    const size_t to_discard = std::min(len, readable);
    if (to_discard == 0)
        return 0;

    _read_pos.store((read_pos + to_discard) % _capacity, std::memory_order_release);
    return to_discard;
}

size_t RingBuffer::peek(void* data, const size_t len, const size_t offset) const {
    if (!data || len == 0)
        return 0;

    const size_t read_pos = _read_pos.load(std::memory_order_relaxed);
    const size_t write_pos = _write_pos.load(std::memory_order_acquire);

    const size_t readable = (write_pos >= read_pos) ? (write_pos - read_pos) : (_capacity - read_pos + write_pos);

    if (readable < offset + len) {
        return 0; // 数据不够
    }

    // 计算实际读取位置（考虑offset）
    const size_t peek_pos = (read_pos + offset) % _capacity;

    // 计算第一段可读取的长度（到缓冲区末尾）
    const size_t first_part = std::min(len, _capacity - peek_pos);
    std::memcpy(data, _buffer.data() + peek_pos, first_part);

    if (first_part < len) {
        std::memcpy(static_cast<uint8_t*>(data) + first_part, _buffer.data(), len - first_part);
    }
    return len;
}

size_t RingBuffer::readable_size() const {
    const size_t read_pos = _read_pos.load(std::memory_order_relaxed);
    const size_t write_pos = _write_pos.load(std::memory_order_acquire);
    return (write_pos >= read_pos) ? (write_pos - read_pos) : (_capacity - read_pos + write_pos);
}

size_t RingBuffer::writable_size() const {
    const size_t read_pos = _read_pos.load(std::memory_order_acquire);
    const size_t write_pos = _write_pos.load(std::memory_order_relaxed);
    return (read_pos > write_pos) ? (read_pos - write_pos - 1) : (_capacity - write_pos + read_pos);
}

void RingBuffer::clear() {
    _read_pos.store(0, std::memory_order_release);
    _write_pos.store(0, std::memory_order_release);
}
