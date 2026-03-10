//
// Created by pengx on 2025/9/26.
//

#ifndef GB28181CONSOLE_SDP_PARSER_HPP
#define GB28181CONSOLE_SDP_PARSER_HPP

#include <map>
#include <regex>
#include <string>

#include "logger.hpp"

struct SdpStruct {
    std::string remote_host;
    int remote_port = 0;
    std::string media_type = "video";
    std::map<int, std::string> rtp_map; //a=rtpmap:96 PS/90000
    std::string transport = "tcp";      // "udp" or "tcp"
    std::string ssrc;                   // 流标识
    std::string setup;                  // 被动/主动
};

class SdpParser {
public:
    explicit SdpParser();

    static SdpParser* get() {
        static SdpParser instance;
        return &instance;
    }

    SdpParser(const SdpParser&) = delete;

    SdpParser& operator=(const SdpParser&) = delete;

    SdpStruct parse(const std::string& sdp);

    /**
     * 构建上行推流 SDP Answer
     * @param device_code 设备编码
     * @param local_ip
     * @param ssrc 流标识
     */
    std::string buildUpstreamSdp(const std::string& device_code,
                                 const std::string& local_ip,
                                 const std::string& ssrc);

    /**
     * 构建下行音频 SDP Answer
     * @param device_code 设备编码
     * @param local_ip
     * @param local_port
     * @param alaw
     */
    std::string buildDownstreamSdp(const std::string& device_code,
                                   const std::string& local_ip,
                                   uint16_t local_port,
                                   bool alaw);

private:
    Logger _logger;
    SdpStruct _sdp_struct{};
};


#endif //GB28181CONSOLE_SDP_PARSER_HPP
