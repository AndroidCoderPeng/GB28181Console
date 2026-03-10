//
// Created by peng on 2025/12/20.
//

#include "rtp_sender.hpp"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <random>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "utils.hpp"

RtpSender::RtpSender() : _logger("RtpSender") {
    _logger.i("RtpSender created");
}

bool RtpSender::initTcpSocket(const SdpStruct& sdp) {
    auto cleanup_socket = [&]() {
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
        _logger.e("创建 TCP socket 失败");
        return false;
    }

    // 设置为非阻塞
    int flags = fcntl(_rtp_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(_rtp_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        _logger.e("设置 socket 为非阻塞模式失败");
        cleanup_socket();
        return false;
    }

    // 填充远程地址（平台 IP + TCP 端口）
    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(sdp.remote_port);
    if (inet_pton(AF_INET, sdp.remote_host.c_str(), &remote_addr.sin_addr) <= 0) {
        _logger.eFmt("无效的目标主机地址: %s", sdp.remote_host.c_str());
        cleanup_socket();
        return false;
    }

    // 主动连接平台
    int ret = connect(_rtp_socket, reinterpret_cast<struct sockaddr*>(&remote_addr), sizeof(remote_addr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            _logger.eFmt("连接到 %s:%d 失败，错误代码: %d", sdp.remote_host.c_str(), sdp.remote_port,errno);
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
            _logger.eFmt("连接超时或失败：%s:%d", sdp.remote_host.c_str(), sdp.remote_port);
            cleanup_socket();
            return false;
        }

        // 检查socket错误状态
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(_rtp_socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            _logger.eFmt("连接失败，socket错误: %d", error);
            cleanup_socket();
            return false;
        }
    }

    init_ssrc_seq(sdp.ssrc);
    _is_tcp = true;
    _logger.dBox()
           .add("成功连接")
           .addFmt("目标地址: %s:%d", sdp.remote_host.c_str(), sdp.remote_port)
           .print();
    return true;
}

bool RtpSender::initUdpSocket(const SdpStruct& sdp) {
    auto cleanup_socket = [&]() {
        if (_rtp_socket >= 0) {
            close(_rtp_socket);
            _rtp_socket = -1;
        }
    };

    // 如果已有socket，先关闭
    if (_rtp_socket > 0) {
        cleanup_socket();
    }

    // 创建 UDP socket
    _rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (_rtp_socket < 0) {
        _logger.e("创建 UDP socket 失败");
        return false;
    }

    // 设置为非阻塞
    const int flags = fcntl(_rtp_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(_rtp_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        _logger.e("设置 socket 为非阻塞模式失败");
        cleanup_socket();
        return false;
    }

    // 设置发送缓冲区大小
    constexpr int send_buffer_size = 512 * 1024; // 512KB
    setsockopt(_rtp_socket, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));

    // 填充远程地址
    memset(&_remote_addr, 0, sizeof(_remote_addr));
    _remote_addr.sin_family = AF_INET;
    _remote_addr.sin_port = htons(sdp.remote_port);
    if (inet_pton(AF_INET, sdp.remote_host.c_str(), &_remote_addr.sin_addr) <= 0) {
        _logger.eFmt("无效的目标主机地址: %s", sdp.remote_host.c_str());
        cleanup_socket();
        return false;
    }

    init_ssrc_seq(sdp.ssrc);
    _is_tcp = false;
    _logger.i("UDP socket 初始化成功");
    return true;
}

void RtpSender::init_ssrc_seq(const std::string& ssrc) {
    std::string ssrc_str;
    if (!ssrc.empty()) {
        ssrc_str = ssrc;
    } else {
        ssrc_str = Utils::get()->randomSsrc();
    }

    // 在 GB28181 里通常是十进制 32 位 SSRC，所以应按十进制解析
    try {
        size_t pos = 0;
        const unsigned long ssrc_val = std::stoul(ssrc_str, &pos, 10);
        // 验证整个字符串都被成功解析
        if (pos != ssrc_str.length()) {
            _logger.eFmt("SSRC 格式无效: %s", ssrc_str.c_str());
            _ssrc = static_cast<uint32_t>(std::stoul(Utils::get()->randomSsrc(), nullptr, 10));
        } else {
            _ssrc = static_cast<uint32_t>(ssrc_val);
        }
    } catch (const std::exception& e) {
        _logger.eFmt("SSRC 解析失败: %s, 使用随机值", e.what());
        _ssrc = static_cast<uint32_t>(std::stoul(Utils::get()->randomSsrc(), nullptr, 10));
    }

    // 初始化 seq 为随机值
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dis16(0, 0xFFFF);
    _seq = dis16(gen);

    _logger.dBox()
           .addFmt("ssrc str: %s", ssrc_str.c_str())
           .addFmt("ssrc    : 0x%08X", _ssrc)
           .addFmt("seq     : %u", _seq)
           .print();
}

RtpSender::~RtpSender() {
    if (_rtp_socket > 0) {
        close(_rtp_socket);
        _rtp_socket = -1;
    }
}

void RtpSender::sendDataPacket(const uint8_t* pkt, const size_t pkt_len, const bool is_end, const uint32_t timestamp) {
    if (pkt == nullptr || pkt_len == 0 || pkt_len > MAX_RTP_PAYLOAD) {
        _logger.eFmt("Invalid packet data: pkt=%p, len=%zu", pkt, pkt_len);
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

void RtpSender::send_packet(const uint8_t* rtp_packet, const size_t rtp_len) {
    if (_is_tcp) {
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
                }
                _logger.eFmt("TCP 发送 RTP 头部失败: %d", errno);
                return;
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
                }
                _logger.eFmt("TCP 发送 RTP 数据失败，已发送 %zu/%zu 字节，错误: %d", total_sent,
                             rtp_len,
                             errno);
                return;
            }
            total_sent += sent;
        }

        if (total_sent != rtp_len) {
            _logger.eFmt("TCP 数据发送不完整，期望 %zu 字节，实际发送 %zu 字节", rtp_len, total_sent);
        }
    } else {
        const ssize_t sent = sendto(_rtp_socket,
                                    rtp_packet,
                                    rtp_len,
                                    MSG_NOSIGNAL,
                                    reinterpret_cast<struct sockaddr*>(&_remote_addr),
                                    sizeof(_remote_addr));
        if (sent < 0) {
            _logger.eFmt("UDP 发送 RTP 数据失败，错误: %d (%s)", errno, strerror(errno));
        } else if (static_cast<size_t>(sent) != rtp_len) {
            _logger.eFmt("UDP 发送字节数不匹配，期望 %zu，实际 %zd", rtp_len, sent);
        }
    }
}

void RtpSender::stop() {
    if (_rtp_socket > 0) {
        close(_rtp_socket);
        _rtp_socket = -1;
    }
}
