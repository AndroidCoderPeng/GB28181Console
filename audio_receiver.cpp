//
// Created by pengx on 2026/2/10.
//

#include "audio_receiver.hpp"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <utility>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

// G.711参数：每帧160字节 = 20ms @ 8kHz采样，8bit量化
#define G711_FRAME_SIZE 160
#define RING_BUFFER_SIZE (256 * 1024)  // 256KB环形缓冲

AudioReceiver::~AudioReceiver() {
    stop();
}

int AudioReceiver::initialize() {
    _receive_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_receive_socket_fd < 0) {
        std::cerr << "创建 TCP socket 失败" << strerror(errno) << std::endl;
        return -1;
    }

    // 绑定到指定端口
    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0);

    if (bind(_receive_socket_fd, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
        std::cerr << "绑定 TCP socket 失败" << strerror(errno) << std::endl;
        close(_receive_socket_fd);
        _receive_socket_fd = -1;
        return -1;
    }

    // 获取系统分配的实际端口号
    int audio_port;
    sockaddr_in bound_addr{};
    socklen_t addr_len = sizeof(bound_addr);
    if (getsockname(_receive_socket_fd, reinterpret_cast<sockaddr*>(&bound_addr), &addr_len) == 0) {
        audio_port = ntohs(bound_addr.sin_port);
    } else {
        std::cerr << "获取 socket 名称失败" << strerror(errno) << std::endl;
        return -1;
    }

    // 设置为非阻塞模式
    const int flags = fcntl(_receive_socket_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(_receive_socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "设置非阻塞模式失败" << strerror(errno) << std::endl;
        close(_receive_socket_fd);
        _receive_socket_fd = -1;
        return -1;
    }

    // 设置接收缓冲区大小
    constexpr int rcv_buf_size = 256 * 1024; // 256KB
    if (setsockopt(_receive_socket_fd, SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, sizeof(rcv_buf_size)) < 0) {
        std::cerr << "设置接收缓冲区失败" << strerror(errno) << std::endl;
    }

    std::cout << "AudioReceiver socket 初始化成功，端口: " << audio_port << "，环形缓冲区: " << RING_BUFFER_SIZE / 1024 << " KB" <<
            std::endl;
    return audio_port;
}

bool AudioReceiver::connectPlatform(const std::string& server_ip, uint16_t server_port) const {
    if (_receive_socket_fd <= 0) {
        std::cerr << "socket 未初始化" << std::endl;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) != 1) {
        std::cerr << "无效的服务器地址: " << server_ip << std::endl;
        return false;
    }

    int ret = connect(_receive_socket_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        std::cerr << "连接平台失败: " << strerror(errno) << std::endl;
        return false;
    }

    // 如果连接正在进行,等待连接完成
    if (ret < 0 && errno == EINPROGRESS) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(_receive_socket_fd, &write_fds);

        timeval timeout{};
        timeout.tv_sec = 5; // 5秒超时
        timeout.tv_usec = 0;

        int select_ret = select(_receive_socket_fd + 1, nullptr, &write_fds, nullptr, &timeout);
        if (select_ret <= 0) {
            std::cerr << "连接超时或失败" << strerror(errno) << std::endl;
            return false;
        }

        // 检查socket错误
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(_receive_socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            std::cerr << "连接失败: " << strerror(error) << std::endl;
            return false;
        }
    }

    std::cout << "已连接平台 " << server_ip << ":" << server_port << std::endl;
    return true;
}

void AudioReceiver::start(AudioDataCallback callback) {
    if (_receive_socket_fd <= 0) {
        std::cerr << "socket 未初始化" << std::endl;
        return;
    }

    if (_is_thread_running) {
        stop(); // 先停止之前的接收
    }

    _audio_callback = std::move(callback);
    _is_thread_running = true;
    _frame_buffer.reserve(G711_FRAME_SIZE * 10); // 预分配空间

    _receive_thread = std::thread(&AudioReceiver::data_receive_loop, this);
    std::cout << "接收线程已启动" << std::endl;
}

void AudioReceiver::data_receive_loop() {
    std::cout << "接收循环开始" << std::endl;

    // 临时接收缓冲区（8KB，避免频繁系统调用）
    std::vector<uint8_t> temp_buffer(8192);

    while (_is_thread_running.load()) {
        if (_receive_socket_fd <= 0) {
            std::cerr << "socket已关闭，退出接收循环" << std::endl;
            break;
        }

        // 直接读取到环形缓冲区
        size_t writable = _ring_buffer.writable_size();
        if (writable < 2048) {
            std::cout << "环形缓冲区使用率过高，丢弃旧数据" << std::endl;
            _ring_buffer.discard(RING_BUFFER_SIZE / 4); // 丢弃25%
            writable = _ring_buffer.writable_size();
        }

        // 读取数据到临时缓冲区，然后写入环形缓冲
        const int to_read = std::min(temp_buffer.size(), writable);
        const int received = recv(_receive_socket_fd, temp_buffer.data(), to_read, MSG_DONTWAIT);

        if (received > 0) {
            // 写入环形缓冲区
            const size_t written = _ring_buffer.write(temp_buffer.data(), received);
            if (written != received) {
                std::cerr << "环形缓冲区写入不完整: " << written << "/" << received << std::endl;
            }
            // 处理积累的音频帧
            handle_audio_frames();
        } else if (received == 0) {
            std::cout << "连接被平台关闭，退出接收循环" << std::endl;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞正常情况，稍微休眠避免 CPU 占用过高
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (_is_thread_running.load()) {
                std::cout << "接收错误: " << strerror(errno) << " (fd=" << _receive_socket_fd << ")" << std::endl;
            }

            // 如果是致命错误，退出循环
            if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK) {
                std::cerr << "socket 已关闭或无效，退出接收循环" << std::endl;
                break;
            }

            // 连接相关错误：立即退出
            if (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE) {
                std::cerr << "连接已断开，退出接收循环" << std::endl;
                break;
            }

            // 其他临时错误：休眠后重试，避免忙等待
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::cout << "接收线程已退出，总计处理 " << _frame_count.load() << " 帧" << std::endl;
}

/**
 * 平台音频结构：[03 2c][RTP头(12字节)][音频数据(多个160字节帧)]...
 * */
void AudioReceiver::handle_audio_frames() {
    if (!_audio_callback)
        return;

    while (true) {
        // 搜索 RTP 头标记 0x80 0x88
        size_t offset = 0;
        bool found = false;

        while (offset + 14 <= _ring_buffer.readable_size()) {
            uint8_t header[14];
            if (_ring_buffer.peek(header, 14, offset) != 14)
                break;

            // 查找 03 2c 80 88 模式
            if (header[0] == 0x03 && header[1] == 0x2c && header[2] == 0x80 && header[3] == 0x88) {
                found = true;
                break;
            }
            offset++;
        }

        if (!found) {
            const size_t readable = _ring_buffer.readable_size();
            if (readable > 2048) {
                std::cout << "缓冲区堆积" << readable << "字节，丢弃前1024字节" << std::endl;
                _ring_buffer.discard(1024);
            }
            break;
        }

        if (offset > 0) {
            // 先处理 offset 之前的音频数据（如果有的话）
            while (offset >= G711_FRAME_SIZE) {
                _frame_buffer.resize(G711_FRAME_SIZE);
                _ring_buffer.read(_frame_buffer.data(), G711_FRAME_SIZE);
                _audio_callback(_frame_buffer.data(), G711_FRAME_SIZE);
                offset -= G711_FRAME_SIZE;
                _frame_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (offset > 0) {
                _ring_buffer.discard(offset);
            }
        }

        // 跳过 03 2c（2字节）+ RTP头（12字节）
        if (_ring_buffer.readable_size() < 14)
            break;
        _ring_buffer.discard(14);

        // 读取160字节音频
        if (_ring_buffer.readable_size() < G711_FRAME_SIZE)
            break;

        _frame_buffer.resize(G711_FRAME_SIZE);
        const size_t read_bytes = _ring_buffer.read(_frame_buffer.data(), G711_FRAME_SIZE);

        if (read_bytes == G711_FRAME_SIZE) {
            _audio_callback(_frame_buffer.data(), G711_FRAME_SIZE);
            _frame_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void AudioReceiver::stop() {
    if (_is_thread_running) {
        _is_thread_running = false;

        // 先关闭 socket 以触发 recv 返回 EBADF
        if (_receive_socket_fd > 0) {
            shutdown(_receive_socket_fd, SHUT_RDWR);
            close(_receive_socket_fd);
            _receive_socket_fd = -1;
        }

        // 等待线程结束
        if (_receive_thread.joinable()) {
            _receive_thread.join();
        }

        // 清空缓冲区
        _ring_buffer.clear();
        _frame_buffer.clear();
        _frame_count = 0;

        std::cout << "接收线程已停止" << std::endl;
    }
}