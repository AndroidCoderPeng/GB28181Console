//
// Created by peng on 2025/12/20.
//

#ifndef GB28181_RTP_SENDER_HPP
#define GB28181_RTP_SENDER_HPP

#include <netinet/in.h>

#include "sdp_parser.hpp"

class RtpSender {
public:
    explicit RtpSender() = default;

    static RtpSender* get() {
        static RtpSender instance;
        return &instance;
    }

    RtpSender(const RtpSender&) = delete;

    RtpSender& operator=(const RtpSender&) = delete;

    bool initialize(const SdpStruct& sdp);

    /**
     * 发送 PS 数据包
     *
     * @param pkt 数据包
     * @param pkt_len 数据包长度
     * @param is_end 数据包是否结束
     * @param timestamp 时间戳
     */
    void sendDataPacket(const uint8_t* pkt, size_t pkt_len, bool is_end, uint32_t timestamp);

    void stop();

    ~RtpSender();

private:
    int _rtp_socket = -1; // 用于发送数据的socket
    uint32_t _ssrc = 0x12345678;
    uint16_t _seq = 0;
    uint8_t _payload_type = 96; // PS流的 payload type

    /**
     * 初始化 SSRC 和 Seq
     *
     * @param sdp SDP
     */
    void init_ssrc_seq(const SdpStruct& sdp);

    /**
     * 填充 RTP 头
     *
     * @param packet RTP 头
     * @param marker_bit 标记位
     * @param timestamp 时间戳
     */
    void fill_rtp_header(uint8_t* packet, bool marker_bit, uint32_t timestamp) const;

    /**
     * 发送 RTP 包
     *
     * @param rtp_packet RTP 包
     * @param rtp_len RTP 包长度
     */
    void send_packet(const uint8_t* rtp_packet, size_t rtp_len) const;
};

#endif //GB28181_RTP_SENDER_HPP
