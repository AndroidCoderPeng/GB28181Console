//
// Created by pengx on 2025/9/26.
//

#include "sdp_parser.hpp"

#include <iostream>
#include <sstream>

SdpStruct SdpParser::parse(const std::string &sdp) {
    SdpStruct sdp_struct{};

    // 提取 c= 行的 IP【c=IN IP4 111.198.10.15】
    std::regex c_regex("c=IN IP4 ([\\d\\.]+)");
    std::smatch c_match;
    if (regex_search(sdp, c_match, c_regex) && c_match.size() > 1) {
        sdp_struct.remote_host = c_match[1].str();
    }

    // 提取 m= 行【m=video 30465 TCP/RTP/AVP 96 97 98】
    std::regex m_line_regex(R"(m=(\w+)\s+(\d+)\s+([\w/]+))");
    std::smatch m_match;
    if (std::regex_search(sdp, m_match, m_line_regex) && m_match.size() > 3) {
        sdp_struct.media_type = m_match[1].str();
        sdp_struct.remote_port = std::stoi(m_match[2].str());
        std::string proto = m_match[3].str();

        // 判断是 TCP 还是 UDP
        if (proto.find("TCP") != std::string::npos) {
            sdp_struct.transport = "tcp";
        } else {
            sdp_struct.transport = "udp";
        }
    }

    // === 解析 a=setup:... ===【a=setup:passive】
    std::regex setup_regex(R"(a=setup:(\w+))");
    std::smatch setup_match;
    if (std::regex_search(sdp, setup_match, setup_regex) && setup_match.size() > 1) {
        sdp_struct.setup = setup_match[1].str();
    }

    // 解析所有 a=rtpmap: 条目【a=rtpmap:96 PS/90000】
    std::regex rtpmap_regex(R"(a=rtpmap:(\d+)\s+([\w/]+)/(\d+))");
    auto rtpmap_begin = std::sregex_iterator(sdp.begin(), sdp.end(), rtpmap_regex);
    auto rtpmap_end = std::sregex_iterator();

    for (std::sregex_iterator i = rtpmap_begin; i != rtpmap_end; ++i) {
        const std::smatch &match = *i;
        if (match.size() > 3) {
            int payload_type = std::stoi(match[1].str());
            std::string encoding = match[2].str();
            sdp_struct.rtpmap[payload_type] = encoding;
        }
    }

    // 解析所有 a=fmtp: 条目
    std::regex fmtp_regex(R"(a=fmtp:(\d+)\s+(.+))");
    auto fmtp_begin = std::sregex_iterator(sdp.begin(), sdp.end(), fmtp_regex);
    auto fmtp_end = std::sregex_iterator();

    for (std::sregex_iterator i = fmtp_begin; i != fmtp_end; ++i) {
        const std::smatch &match = *i;
        if (match.size() > 2) {
            int payload_type = std::stoi(match[1].str());
            std::string format_params = match[2].str();
            sdp_struct.fmtp[payload_type] = format_params;
        }
    }

    // y= 字段（GB28181 SSRC）【y=0108000147】
    std::regex y_regex(R"(y=(\S+))");
    std::smatch y_match;
    if (std::regex_search(sdp, y_match, y_regex) && y_match.size() > 1) {
        sdp_struct.ssrc = y_match[1].str();
    }

    return sdp_struct;
}

std::string SdpParser::buildSdpAnswer(const std::string &device_code, const std::string &local_ip, const int local_port,
                                      const std::string &ssrc, const std::string &transport) {
    std::ostringstream sdp;
    sdp << "v=0\r\n"
            << "o=" << device_code << " 0 0 IN IP4 " << local_ip << "\r\n"
            << "s=Play\r\n"
            << "c=IN IP4 " << local_ip << "\r\n"
            << "t=0 0\r\n";

    if (transport == "tcp") {
        sdp << "m=video 0 TCP/RTP/AVP 96\r\n"
                << "a=sendonly\r\n"
                << "a=rtpmap:96 PS/90000\r\n"
                << "a=setup:active\r\n" // 主动连平台
                << "y=" << ssrc << "\r\n";
    } else {
        sdp << "m=video " << local_port << " RTP/AVP 96\r\n"
                << "a=sendonly\r\n"
                << "a=rtpmap:96 PS/90000\r\n"
                << "y=" << ssrc << "\r\n";
    }

    return sdp.str();
}
