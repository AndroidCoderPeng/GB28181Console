//
// Created by pengx on 2025/9/26.
//

#include "sdp_parser.hpp"
#include "utils.hpp"

#include <iostream>
#include <sstream>

SdpStruct SdpParser::parse(const std::string& sdp) {
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
        const std::smatch& match = *i;
        if (match.size() > 3) {
            int payload_type = std::stoi(match[1].str());
            std::string encoding = match[2].str();
            sdp_struct.rtp_map[payload_type] = encoding;
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

/**
 * 上行数据是PS流，内部包含H.264视频和G.711音频，一个m=video行足以描述整个PS流
 * 下行数据是G.711音频，那就还需要一个m=audio行
 *
 * a=sendonly: 只发送媒体流，不接收
 * a=recvonly: 只接收媒体流，不发送
 * a=sendrecv: 既能发送又能接收媒体流（双向通信）
 * a=inactive: 不发送也不接收媒体流
 *
 * 1. 点对平台场景
 *  - 设备推流: 使用 a=sendonly（设备→平台）
 *  - 语音对讲: 使用 a=recvonly（平台→设备）
 *
 * 2. 点对点场景（需要 a=sendrecv: 每个终端既是发送方也是接收方）
 *  - 视频会议: 双方都需要发送和接收音视频
 *  - P2P通话: 双向实时通信，双方角色对等
 * */
std::string SdpParser::buildUpstreamSdp(const std::string& device_code, const std::string& local_ip,
                                        const std::string& ssrc) {
    std::ostringstream oss;
    //o=<username> <sess-id> <sess-version> IN IP4 <unicast-address>
    oss << "v=0\r\n";
    oss << "o=" << device_code << " 0 0 IN IP4 " << local_ip << "\r\n";
    oss << "s=Play\r\n"
            << "c=IN IP4 " << local_ip << "\r\n"
            << "t=0 0\r\n";
    oss << "m=video 9 TCP/RTP/AVP 96\r\n" // 端口号必须写 9（这是约定值，表示“复用信令连接”，不是本地端口！）
            << "a=sendonly\r\n"
            << "a=rtpmap:96 PS/90000\r\n"
            << "a=connection:new\r\n"
            << "y=" << ssrc << "\r\n";

    std::string result = oss.str();

    std::cout << "┌─────────── Upstream SDP ─────────────┐" << std::endl;
    std::cout << result.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return result;
}

std::string SdpParser::buildDownstreamSdp(const std::string& device_code, const std::string& local_ip,
                                          const uint16_t local_port, const bool alaw) {
    std::ostringstream oss;
    const std::string ssrc = Utils::randomSsrc();
    oss << "v=0\r\n";
    oss << "o=" << device_code << " 0 0 IN IP4 " << local_ip << "\r\n";
    oss << "s=Play\r\n"
            << "c=IN IP4 " << local_ip << "\r\n"
            << "t=0 0\r\n";
    if (alaw) {
        oss << "m=audio " << local_port << " TCP/RTP/AVP 8 96\r\n"
                << "a=setup:active\r\n"
                << "a=rtpmap:8 PCMA/8000\r\n";
    } else {
        oss << "m=audio " << local_port << " TCP/RTP/AVP 0 96\r\n"
                << "a=setup:active\r\n"
                << "a=rtpmap:0 PCMU/8000\r\n";
    }
    oss << "a=rtpmap:96 PS/90000\r\n"
            << "a=recvonly\r\n"
            << "f=v/////a/1/8/1\r\n"
            << "y=" << ssrc << "\r\n";

    /**
     * f=v/////a/1/8/1
     *   │  │  │ │ │ │
     *   │  │  │ │ │ └─ 音频是否VBR: 1=固定码率(CBR)
     *   │  │  │ │ └─── 音频采样位数: 8=8位
     *   │  │  │ └───── 音频通道数: 1=单声道
     *   │  │  └─────── a=音频标识
     *   │  └────────── 视频分辨率、帧率等参数（空表示不关心）
     *   └───────────── v=视频标识（后面4个/是占位符）
     * */

    std::string result = oss.str();

    std::cout << "┌────────── Downstream SDP ────────────┐" << std::endl;
    std::cout << result.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return result;
}
