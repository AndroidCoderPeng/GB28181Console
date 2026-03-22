//
// Created by pengx on 2026/2/10.
//

#ifndef GB28181CONSOLE_RING_BUFFER_HPP
#define GB28181CONSOLE_RING_BUFFER_HPP

#include <atomic>
#include <cstring>
#include <vector>

class RingBuffer {
public:
    /**
     * @brief 构造函数
     * @param capacity 缓冲区容量
     */
    explicit RingBuffer(size_t capacity);

    /**
     * @brief 写入数据
     * @param data 数据
     * @param len 数据长度
     * @return 实际写入的长度
     */
    size_t write(const uint8_t* data, size_t len);

    /**
     * @brief 读取数据
     * @param data 数据
     * @param len 数据长度
     * @return 读取的长度
     */
    size_t read(uint8_t* data, size_t len);

    /**
     * @brief 丢弃数据
     * @param len 数据长度
     * @return 丢弃的长度
     */
    size_t discard(size_t len);

    /**
     * @brief 窥探数据
     * @param data 数据
     * @param len 数据长度
     * @param offset 偏移量
     * @return 窥探的长度
     */
    size_t peek(void* data, size_t len, size_t offset = 0) const;

    /**
     * @brief 获取可读数据长度
     * @return 可读数据长度
     */
    size_t readable_size() const;

    /**
     * @brief 获取可写数据长度
     * @return 可写数据长度
     */
    size_t writable_size() const;

    /**
    * @brief 清空数据
    */
    void clear();

    /**
    * @brief 数据是否为空
     * */
    bool empty() const {
        return readable_size() == 0;
    }

    /**
    * @brief 数据是否已满
     * */
    bool full() const {
        return writable_size() == 0;
    }

private:
    std::vector<uint8_t> _buffer;
    size_t _capacity;
    std::atomic<size_t> _read_pos{0};
    std::atomic<size_t> _write_pos{0};
};

#endif //GB28181CONSOLE_RING_BUFFER_HPP