//
// Created by pengx on 2025/9/26.
//

#ifndef GB28181_SDP_PARSER_HPP
#define GB28181_SDP_PARSER_HPP

#include <string>
#include <regex>
#include <map>

struct SdpStruct {
    std::string remote_host;
    int remote_port = 0;
    std::string media_type = "video";
    std::map<int, std::string> rtpmap; //a=rtpmap:96 PS/90000
    std::map<int, std::string> fmtp; //a=fmtp:126 profile-level-id=42e01e
    std::string ssrc; // 流标识
    std::string transport = "udp"; // "udp" or "tcp"
    std::string setup; // "active", "passive", or empty 被动/主动/其他
};

class SdpParser {
public:
    static SdpStruct parse(const std::string &sdp);

    /**
     * 构建 SDP Answer
     * @param device_code 设备编码
     * @param local_ip 本地 IP
     * @param local_port 本地端口
     * @param ssrc 流标识
     * @param transport 传输协议
     *
     * - 不使用 PS 封装，而是采用裸 H.264 + G.711 分离 RTP 流，才需要如下回复
     * - m=video + a=rtpmap:98 H264/90000 + a=fmtp:98 profile-level-id=...
     * - m=audio + a=rtpmap:8 PCMA/8000
     */
    static std::string buildSdpAnswer(const std::string &device_code, const std::string &local_ip, int local_port,
                                      const std::string &ssrc, const std::string &transport = "udp");
};


#endif //GB28181_SDP_PARSER_HPP
