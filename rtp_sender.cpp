//
// Created by peng on 2025/12/20.
//

#include "rtp_sender.hpp"
#include "utils.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <iomanip>
#include <random>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <chrono>
#include <thread>

bool RtpSender::initialize(const SdpStruct& sdp) {
    auto cleanup_socket = [&] {
        if (_rtp_socket >= 0) {
            close(_rtp_socket);
            _rtp_socket = -1;
        }
    };

    // 如果已有socket，先关闭
    if (_rtp_socket > 0) {
        cleanup_socket();
    }

    // 创建 TCP socket
    _rtp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_rtp_socket < 0) {
        std::cerr << "创建 TCP socket 失败" << std::endl;
        return false;
    }

    int send_buf_size = 512 * 1024; // 512KB
    if (setsockopt(_rtp_socket, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size)) < 0) {
        std::cerr << "设置发送缓冲区大小失败" << std::endl;
    }

    // 设置为非阻塞
    const int flags = fcntl(_rtp_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(_rtp_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "设置 socket 为非阻塞模式失败" << std::endl;
        cleanup_socket();
        return false;
    }

    // 填充远程地址（平台 IP + TCP 端口）
    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(sdp.remote_port);
    if (inet_pton(AF_INET, sdp.remote_host.c_str(), &remote_addr.sin_addr) <= 0) {
        std::cerr << "无效的目标主机地址: " << sdp.remote_host << std::endl;
        cleanup_socket();
        return false;
    }

    // 主动连接平台
    int ret = connect(_rtp_socket, reinterpret_cast<sockaddr*>(&remote_addr), sizeof(remote_addr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            std::cerr << "连接到 " << sdp.remote_host.c_str() << ":" << sdp.remote_port << " 失败，错误代码: " << errno <<
                    std::endl;
            cleanup_socket();
            return false;
        }

        // 非阻塞连接需要等待完成
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(_rtp_socket, &write_fds);

        timeval timeout{};
        timeout.tv_sec = 5; // 5秒超时
        timeout.tv_usec = 0;

        ret = select(_rtp_socket + 1, nullptr, &write_fds, nullptr, &timeout);
        if (ret <= 0) {
            std::cerr << "连接超时或失败：" << sdp.remote_host.c_str() << ":" << sdp.remote_port << std::endl;
            cleanup_socket();
            return false;
        }

        // 检查socket错误状态
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(_rtp_socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            std::cerr << "连接失败，socket错误: " << error << std::endl;
            cleanup_socket();
            return false;
        }
    }

    init_ssrc_seq(sdp);

    std::cout << "成功连接：" << sdp.remote_host << ":" << sdp.remote_port << std::endl;
    return true;
}

void RtpSender::init_ssrc_seq(const SdpStruct& sdp) {
    std::string ssrc_str;
    if (!sdp.ssrc.empty()) {
        ssrc_str = sdp.ssrc;
    } else {
        ssrc_str = Utils::randomSsrc();
    }

    // 在 GB28181 里通常是十进制 32 位 SSRC，所以应按十进制解析
    try {
        size_t pos = 0;
        const unsigned long ssrc_val = std::stoul(ssrc_str, &pos, 10);
        // 验证整个字符串都被成功解析
        if (pos != ssrc_str.length()) {
            std::cerr << "SSRC 错误: " << ssrc_str.c_str() << std::endl;
            _ssrc = static_cast<uint32_t>(std::stoul(Utils::randomSsrc(), nullptr, 10));
        } else {
            _ssrc = static_cast<uint32_t>(ssrc_val);
        }
    } catch (const std::exception& e) {
        std::cerr << "SSRC 错误: " << ssrc_str.c_str() << ", 使用随机值" << std::endl;
        _ssrc = static_cast<uint32_t>(std::stoul(Utils::randomSsrc(), nullptr, 10));
    }

    // 初始化 seq 为随机值
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dis16(0, 0xFFFF);
    _seq = dis16(gen);

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  ssrc_str: " << ssrc_str.c_str() << std::endl;
    std::cout << "│  ssrc    : " << _ssrc << std::endl;
    std::cout << "│  seq     : " << _seq << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
}

RtpSender::~RtpSender() {
    if (_rtp_socket > 0) {
        close(_rtp_socket);
        _rtp_socket = -1;
    }
}

void RtpSender::sendDataPacket(const uint8_t* pkt, const size_t pkt_len, const bool is_end, const uint32_t timestamp) {
    if (pkt == nullptr || pkt_len == 0 || pkt_len > MAX_RTP_PAYLOAD) {
        std::cerr << "Invalid packet data: pkt=%p, len=%zu" << pkt << pkt_len << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(_buffer_mutex);

    // 填充 RTP 头 (12 bytes)
    _rtp_buffer[0] = 0x80;
    _rtp_buffer[1] = (is_end ? 0x80 : 0x00) | (_payload_type & 0x7F);
    _rtp_buffer[2] = (_seq >> 8) & 0xFF;
    _rtp_buffer[3] = _seq & 0xFF;
    _rtp_buffer[4] = (timestamp >> 24) & 0xFF;
    _rtp_buffer[5] = (timestamp >> 16) & 0xFF;
    _rtp_buffer[6] = (timestamp >> 8) & 0xFF;
    _rtp_buffer[7] = timestamp & 0xFF;
    _rtp_buffer[8] = (_ssrc >> 24) & 0xFF;
    _rtp_buffer[9] = (_ssrc >> 16) & 0xFF;
    _rtp_buffer[10] = (_ssrc >> 8) & 0xFF;
    _rtp_buffer[11] = _ssrc & 0xFF;

    // 填充负载
    memcpy(_rtp_buffer + 12, pkt, pkt_len);

    send_packet(_rtp_buffer, 12 + pkt_len);
    _seq++;
}

void RtpSender::send_packet(const uint8_t* rtp_packet, const size_t rtp_len) const {
    const uint8_t header[4] = {
        0x24, // '$'
        0x00, // channel 0 for RTP
        static_cast<uint8_t>((rtp_len >> 8) & 0xFF),
        static_cast<uint8_t>(rtp_len & 0xFF)
    };

    // 发送 header - 确保完整发送
    size_t total_sent = 0;
    while (total_sent < 4) {
        const ssize_t sent = send(_rtp_socket, header + total_sent, 4 - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // 非阻塞模式下的正常情况，继续尝试
            } else {
                std::cerr << "发送 RTP 头部失败: " << strerror(errno) << std::endl;
                return;
            }
        }
        total_sent += sent;
    }

    // 发送 RTP 数据 - 确保完整发送
    total_sent = 0;
    while (total_sent < rtp_len) {
        const ssize_t sent = send(_rtp_socket,
                                  rtp_packet + total_sent,
                                  rtp_len - total_sent,
                                  MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // 非阻塞模式下的正常情况，继续尝试
            } else {
                std::cerr << "发送 RTP 数据失败，已发送 " << total_sent / rtp_len << " 字节，错误: " << strerror(errno) << std::endl;
                return;
            }
        }
        total_sent += sent;
    }

    if (total_sent != rtp_len) {
        std::cerr << "RTP 数据发送不完整，期望 " << rtp_len << " 字节，实际发送 " << total_sent << " 字节" << std::endl;
    }
}

void RtpSender::stop() {
    if (_rtp_socket > 0) {
        close(_rtp_socket);
        _rtp_socket = -1;
    }
}
