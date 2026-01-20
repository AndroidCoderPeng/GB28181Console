//
// Created by peng on 2025/12/20.
//

#ifndef GB28181_RTP_SENDER_HPP
#define GB28181_RTP_SENDER_HPP

#include <netinet/in.h>

#include "sdp_parser.hpp"

class RtpSender
{
public:
    explicit RtpSender() = default;

    static RtpSender* get()
    {
        static RtpSender instance;
        return &instance;
    }

    RtpSender(const RtpSender&) = delete;

    RtpSender& operator=(const RtpSender&) = delete;

    /**
     * 初始化 RTP socket
     *
     * @param sdp SDP
     * @return local port
     */
    int initRtpTcpSocket(const SdpStruct& sdp);

    /**
     * 初始化 RTP socket
     *
     * @param sdp SDP
     * @return local port
     */
    int initRtpUdpSocket(const SdpStruct& sdp);

    /**
     * 初始化 SSRC 和 Seq
     *
     * @param sdp SDP
     */
    void initSsrcSeq(const SdpStruct& sdp);

    /**
     * 发送 PS 数据包
     *
     * @param pkt 数据包
     * @param pkt_len 数据包长度
     * @param is_end 数据包是否结束
     * @param timestamp 时间戳
     */
    void sendDataPacket(const uint8_t* pkt, size_t pkt_len, bool is_end, uint32_t timestamp);

    ~RtpSender();

private:
    bool _is_tcp = false;
    int _rtp_socket = -1;       // 用于发送数据的socket
    sockaddr_in _remote_addr{}; // 远程平台地址信息
    uint32_t _ssrc = 0x12345678;
    uint16_t _seq = 0;
    uint8_t _payload_type = 96; // PS流的 payload type

    void send_tcp_or_udp_packet(const uint8_t* rtp_packet, size_t rtp_len);
};

#endif //GB28181_RTP_SENDER_HPP
