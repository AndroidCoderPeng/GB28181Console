//
// Created by peng on 2025/12/20.
//

#include "rtp_sender.hpp"
#include "base_config.hpp"

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <random>
#include <fcntl.h>
#include <iomanip>

static uint32_t randomSsrc()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    return dis(gen);
}

int RtpSender::initRtpTcpSocket(const SdpStruct& sdp)
{
    if (_rtp_socket > 0)
    {
        close(_rtp_socket);
        _rtp_socket = -1;
    }

    // 创建 TCP socket
    _rtp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_rtp_socket < 0)
    {
        std::cerr << "Create tcp socket failed" << std::endl;
        return -1;
    }

    // 设置为非阻塞（可选，但建议与 UDP 一致）
    const int flags = fcntl(_rtp_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(_rtp_socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Create tcp socket failed" << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    // 填充远程地址（平台 IP + TCP 端口）
    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(sdp.remote_port); // 如 30465
    if (inet_pton(AF_INET, sdp.remote_host.c_str(), &remote_addr.sin_addr) <= 0)
    {
        std::cerr << "Invalid destination host:" << sdp.remote_host << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    // 主动连接平台
    const int ret = connect(_rtp_socket, reinterpret_cast<struct sockaddr*>(&remote_addr), sizeof(remote_addr));
    if (ret < 0)
    {
        if (errno != EINPROGRESS)
        {
            std::cerr << "Connect" << sdp.remote_host << ":" << sdp.remote_port
                << "failed, errno=" << errno << std::endl;
            close(_rtp_socket);
            _rtp_socket = -1;
            return -1;
        }
    }

    // 成功：记录状态
    _is_tcp = true;
    _remote_addr = remote_addr; // 虽然 TCP 不需要，但保留一致性

    initSsrcSeq(sdp);

    std::cout << "Connected to platform TCP:" << sdp.remote_host << ":" << sdp.remote_port << ", SSRC:" << sdp.ssrc;
    return 9; //虽然 TCP 不用端口，但 GB28181 要求写 9（discard port）或任意非零值。写 0 可能被解析为无效。
}

int RtpSender::initRtpUdpSocket(const SdpStruct& sdp)
{
    if (_rtp_socket > 0)
    {
        close(_rtp_socket);
        _rtp_socket = -1;
    }

    // 创建 socket
    _rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (_rtp_socket < 0)
    {
        std::cerr << "Create rtp socket failed" << std::endl;
        return -1;
    }

    // 绑定本地端口（使用端口0让系统自动分配）
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0); // Let OS choose

    if (bind(_rtp_socket, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) < 0)
    {
        std::cerr << "Bind rtp socket failed" << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(_rtp_socket, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) < 0)
    {
        std::cerr << "Allocate local port failed" << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }
    const int local_port = ntohs(bound_addr.sin_port);

    // Set up remote address
    memset(&_remote_addr, 0, sizeof(_remote_addr));
    _remote_addr.sin_family = AF_INET;
    _remote_addr.sin_port = htons(sdp.remote_port);
    if (inet_pton(AF_INET, sdp.remote_host.c_str(), &_remote_addr.sin_addr) <= 0)
    {
        std::cout << "Invalid destination host:" << sdp.remote_host << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    // 设置 Socket 为非阻塞模式
    const int flags = fcntl(_rtp_socket, F_GETFL, 0);
    if (flags == -1)
    {
        std::cerr << "Create rtp socket failed" << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    if (fcntl(_rtp_socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Create rtp socket failed" << std::endl;
        close(_rtp_socket);
        _rtp_socket = -1;
        return -1;
    }

    initSsrcSeq(sdp);

    std::cout << "Platform wants stream:" << sdp.remote_host << ":" << sdp.remote_port
        << ", SSRC:" << sdp.ssrc << ", Local port:" << local_port << std::endl;;
    return local_port;
}

void RtpSender::initSsrcSeq(const SdpStruct& sdp)
{
    if (!sdp.ssrc.empty())
    {
        std::string y_hex = sdp.ssrc;

        // GB28181 的 y= 是十六进制字符串，但某些平台（如 ZLMediaKit）可能发送超过 8 字符（5+ 字节）
        // 根据行业惯例，取低 4 字节（最后 8 个十六进制字符）
        if (y_hex.length() > 8)
        {
            y_hex = y_hex.substr(y_hex.length() - 8);
        }

        // 确保至少有 1 个字符
        if (!y_hex.empty())
        {
            try
            {
                // 按十六进制解析
                _ssrc = static_cast<uint32_t>(std::stoul(y_hex, nullptr, 16));
            }
            catch (const std::exception& e)
            {
                _ssrc = randomSsrc();
                std::cerr << "Failed to parse SSRC from y=" << sdp.ssrc << ", fallback to random" << std::endl;
            }
        }
        else
        {
            _ssrc = randomSsrc();
        }
    }
    else
    {
        _ssrc = randomSsrc();
    }

    // 初始化 seq 为随机值
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis16(0, 0xFFFF);
    _seq = static_cast<uint16_t>(dis16(gen));

    std::cout << "Initialized SSRC from y=" << sdp.ssrc << " -> 0x"
        << std::hex << std::setfill('0') << std::setw(8) << _ssrc << std::endl;;
}

RtpSender::~RtpSender()
{
    if (_rtp_socket > 0)
    {
        close(_rtp_socket);
        _rtp_socket = -1;
    }
}

void RtpSender::sendDataPacket(const uint8_t* pkt, const size_t pkt_len, const bool is_end, const uint32_t timestamp)
{
    if (pkt_len <= MAX_RTP_PAYLOAD)
    {
        // ========== 小包：单个 RTP 包 ==========
        uint8_t packet[MAX_RTP_PACKET]; // 确保 MAX_RTP_PACKET >= 12 + MAX_RTP_PAYLOAD

        // --- 填充 RTP 头 (12 bytes) ---
        packet[0] = 0x80;
        packet[1] = (is_end ? 0x80 : 0x00) | _payload_type & 0x7F;
        packet[2] = _seq >> 8 & 0xFF;
        packet[3] = _seq & 0xFF;
        packet[4] = timestamp >> 24 & 0xFF;
        packet[5] = timestamp >> 16 & 0xFF;
        packet[6] = timestamp >> 8 & 0xFF;
        packet[7] = timestamp & 0xFF;
        packet[8] = _ssrc >> 24 & 0xFF;
        packet[9] = _ssrc >> 16 & 0xFF;
        packet[10] = _ssrc >> 8 & 0xFF;
        packet[11] = _ssrc & 0xFF;

        // --- 填充负载 ---
        memcpy(packet + 12, pkt, pkt_len);

        // 发送（根据 TCP/UDP 模式）
        send_tcp_or_udp_packet(packet, 12 + pkt_len);
        _seq++; // 序列号递增
    }
    else
    {
        // ========== 大包，进行通用分片，在 GB28181 中，不使用 FU-A！ ==========
        size_t offset = 0;
        while (offset < pkt_len)
        {
            const bool last_chunk = (offset + MAX_RTP_PAYLOAD >= pkt_len);
            const size_t chunk_size = last_chunk ? (pkt_len - offset) : MAX_RTP_PAYLOAD;

            uint8_t packet[MAX_RTP_PACKET];

            // --- 填充 RTP 头 ---
            packet[0] = 0x80;
            // 只有最后一个分片且是帧尾时才设 marker
            packet[1] = (last_chunk && is_end ? 0x80 : 0x00) | _payload_type & 0x7F;
            packet[2] = _seq >> 8 & 0xFF;
            packet[3] = _seq & 0xFF;
            packet[4] = timestamp >> 24 & 0xFF;
            packet[5] = timestamp >> 16 & 0xFF;
            packet[6] = timestamp >> 8 & 0xFF;
            packet[7] = timestamp & 0xFF;
            packet[8] = _ssrc >> 24 & 0xFF;
            packet[9] = _ssrc >> 16 & 0xFF;
            packet[10] = _ssrc >> 8 & 0xFF;
            packet[11] = _ssrc & 0xFF;

            // --- 填充负载 ---
            memcpy(packet + 12, pkt + offset, chunk_size);

            send_tcp_or_udp_packet(packet, 12 + chunk_size);
            _seq++; // 每个分片都要递增序列号
            offset += chunk_size;
        }
    }
}

void RtpSender::send_tcp_or_udp_packet(const uint8_t* rtp_packet, const size_t rtp_len)
{
    if (_is_tcp)
    {
        // TCP interleaved 格式: $<channel><lenH><lenL><RTP>
        const uint8_t header[4] = {
            0x24, // '$'
            0x00, // channel 0 for RTP
            static_cast<uint8_t>((rtp_len >> 8) & 0xFF),
            static_cast<uint8_t>(rtp_len & 0xFF)
        };

        // 发送 header
        const ssize_t sent = send(_rtp_socket, header, 4, MSG_NOSIGNAL);
        if (sent == 4)
        {
            // 发送 RTP 包体
            send(_rtp_socket, rtp_packet, rtp_len, MSG_NOSIGNAL);
        }
    }
    else
    {
        // UDP 模式
        sendto(_rtp_socket, rtp_packet, rtp_len, 0, reinterpret_cast<sockaddr*>(&_remote_addr), sizeof(_remote_addr));
    }
}
