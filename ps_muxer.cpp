//
// Created by peng on 2026/1/4.
//

#include "ps_muxer.hpp"
#include "base_config.hpp"
#include "utils.hpp"
#include "header_builder.hpp"
#include "pcm_encoder.hpp"
#include "rtp_sender.hpp"

#include <iostream>

static void print_sps_pps(const std::vector<uint8_t> &sps, const std::vector<uint8_t> &pps) {
    std::string sps_str;
    for (const unsigned char sp: sps) {
        char hex_byte[8];
        snprintf(hex_byte, sizeof(hex_byte), "%02X ", sp);
        sps_str += hex_byte;
    }
    std::cout << "SPS: " << sps_str << std::endl;

    std::string pps_str;
    for (const unsigned char pp: pps) {
        char hex_byte[8];
        snprintf(hex_byte, sizeof(hex_byte), "%02X ", pp);
        pps_str += hex_byte;
    }
    std::cout << "PPS: " << pps_str << std::endl;
}

/**
 * 封装 PS 包，其实就是把 PES 包转为 PS 包，通俗来说就是将音视频同一化，方便后续封装为PS流
 *
 * PS 包 = [起始码] + [PS Header] + [System Header(仅IDR帧)] + [PSM(仅IDR帧)] + [PES 包]
 *
 * @param payload
 * @param len
 * @param pts_90k
 * @param is_key_frame
 * */
static void buildPsPacket(const uint8_t *payload, const size_t len, const uint64_t pts_90k, const bool is_key_frame) {
    // ================================ 添加PS头 ================================//
    auto ps_header = HeaderBuilder::buildPsPackHeader(pts_90k);

    // ================================ 添加系统头和PSM ================================//
    std::vector<uint8_t> config{};
    if (is_key_frame) {
        config = HeaderBuilder::buildSystemHeader();
        auto psm = HeaderBuilder::buildPsm();
        config.insert(config.end(), psm.begin(), psm.end());
    }

    // ================================ 封装 PS 包 ================================//
    // 计算总大小
    const size_t total_size = ps_header.size() + config.size() + len;
    std::vector<uint8_t> ps_pkt(total_size);

    size_t offset = 0;
    std::copy(ps_header.begin(), ps_header.end(), ps_pkt.data() + offset);
    offset += ps_header.size();

    // 添加 System Header 和 PSM（如果存在）
    if (!config.empty()) {
        std::copy(config.begin(), config.end(), ps_pkt.begin() + offset);
        offset += config.size();
    }

    // 添加 PES 载荷数据
    std::copy_n(payload, len, ps_pkt.data() + offset);

    // 打印ps_pkt前32个字节
    std::string ps_pkt_str;
    for (int i = 0; i < 32; i++) {
        char hex_byte[8];
        snprintf(hex_byte, sizeof(hex_byte), "%02X ", ps_pkt[i]);
        ps_pkt_str += hex_byte;
    }
    std::cout << "PS Packet: " << ps_pkt_str << std::endl;

    // 发送PS包。关键帧（I帧）通常是一组帧序列的最后一帧或开始帧
    RtpSender::get()->sendDataPacket(ps_pkt.data(), ps_pkt.size(), is_key_frame, pts_90k);
}

/**
 * 两个概念：
 * PES 和 NALU
 *
 * - NALU：网络应用层单元，即视频帧，是H.264数据的基本单元
 * - PES 包：包含负载数据，以及PTS/DTS等信息，是PS流中的基本单元。视频帧较长需要分割为NALU，音频帧（160字节不用分割），再封装成PES包
 * - 封装 PES 包，其实就是把 整帧的 NALU 或者 G.711μ 数据转为 PES 包，通俗来说就是将音视频同一化，方便后续封装为PS流
 *
 * PES包 = [起始码] + [PES Header] + [负载数据]
 *
 * @param stream_id
 * @param payload 都没有起始码
 * @param len
 * @param pts_90k 必须是90kHz基准的时间戳，否则音画不同步
 * @param is_key_frame
 * */
static void buildPesPacket(const uint8_t stream_id, const uint8_t *payload, const size_t len, const uint64_t pts_90k,
                           const bool is_key_frame) {
    // 前 6 字节（起始码 + stream_id + length）总是存在
    std::vector<uint8_t> pes_header(14); // 一般14字节即可满足需求，如果有私有字段，这个长度也需要变

    // PES start code【3字节】
    pes_header[0] = 0x00;
    pes_header[1] = 0x00;
    pes_header[2] = 0x01;

    // 流ID【1字节】
    pes_header[3] = stream_id;

    // PES 长度【2字节】，PES头之后的数据长度（即负载长度 + 可选头长度）
    const uint16_t pes_len = pes_header.size() - 6 + len;
    pes_header[4] = pes_len >> 8 & 0xFF;
    pes_header[5] = pes_len & 0xFF;

    // ================================ 下面的全是可选字段 ================================//
    /**
     * PES 包第一标志【1字节】，需要按二进制位拆解各位的含义
     *
     * 假设第7字节是 0x84（十六进制），转为二进制是：1000 0100
     * - 位7~6: 10 → 符合 MPEG-2 PES（固定值，用于标识这是一个 MPEG-2 PES 包）
     * - 位5~4: 00 → 未加扰（加密不等于加扰，但可以理解为简单加密，举个例子来说就是就是表示是否收费与免费的区别）
     * - 位3: 0 → 普通优先级（虽不是固定值，但常为0）
     * - 位2: 0 → 无数据对齐（关键帧需要对齐，简单来说关键帧位2=1，其他帧位2=0）
     * - 位1: 1 → 有版权（0或者1随意，自行决定）
     * - 位0: 0 → 是拷贝（首次封装，源头生成位0=1，否则位0=0）
     * */
    if (is_key_frame) {
        // 1000 0111 -> 87
        pes_header[6] = 0x87;
    } else {
        // 1000 0011 -> 83
        pes_header[6] = 0x83;
    }

    /**
     * PES 包第二标志【1字节】，需要按二进制位拆解各位的含义
     *
     * 假设第7字节是 0x80（十六进制），转为二进制是：1000 0000
     * - 位7~6: 10 → 只包含 PTS（绝大多数手机摄像头、实时采集场景都禁用 B 帧，只需 PTS，另外音频永远只需 PTS）
     * - 位5: 0 → 无 ESCR（已经基本废弃不用）
     * - 位4: 0 → 无 ES_rate（已经基本废弃不用）
     * - 位3: 0 → 无 trick mode（已经基本废弃不用）
     * - 位2: 0 → 无 additional copy info（已经基本废弃不用）
     * - 位1: 0 → 有 PES_CRC（基本不用）
     * - 位0: 0 → 扩展字段（私有扩展，基本不用）
     * */
    // 1000 0000 -> 80
    pes_header[7] = 0x80;

    // PES头的附加信息，从第10个字节开始（不包括前9个字节和自己），往后数多少个字节
    pes_header[8] = 0x05;

    // PTS【5字节】，所以附加信息长度就是5。MPEG-2 PTS 只有 33 位，必须 mask 掉高位，避免溢出
    const auto pts = pts_90k & 0x1FFFFFFFFULL;
    pes_header[9] = 0x21 | (pts >> 29 & 0x0E | 0x01);
    pes_header[10] = pts >> 22 & 0xFF;
    pes_header[11] = 0x01 | pts >> 14 & 0xFE;
    pes_header[12] = pts >> 7 & 0xFF;
    pes_header[13] = 0x01 | (pts & 0x7F) << 1;

    // ================================ 封装 PES 包 ================================//
    // 不必管是SPS/PPS/G.711μ/IDR/P，这些都是PES的载荷，载荷位置 = 动态的，由第9字节决定。
    std::vector<uint8_t> pes_pkt(pes_header.size() + len); // pes_header + 负载数据

    // 添加 PES header
    std::copy(pes_header.begin(), pes_header.end(), pes_pkt.begin());

    // 添加载荷数据
    std::copy_n(
        payload, len, pes_pkt.begin() + static_cast<std::vector<uint8_t>::difference_type>(pes_header.size())
    );

    // 封装PS包
    buildPsPacket(pes_pkt.data(), pes_pkt.size(), pts_90k, is_key_frame);
}

/**
 * 视频帧：H.264，时间基已经设置为90kHz
 * */
void PsMuxer::writeVideoFrame(const uint8_t *h264_data, const uint64_t pts_90k, const int size) {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    // 一个 cv::Mat 经过编码后可能包含多种NALU类型，但是至多只有一个IDR
    const auto nalUnits = Utils::splitNalUnits(h264_data, size);

    std::vector<uint8_t> sps{};
    std::vector<uint8_t> pps{};
    const std::vector<uint8_t> *idr = nullptr;

    // 解析NALUs并识别SPS/PPS/IDR
    for (const auto &nalu: nalUnits) {
        if (nalu.empty()) continue;
        const uint8_t nalType = nalu[0] & 0x1F;
        switch (nalType) {
            case 5: // IDR帧
                idr = &nalu;
                break;
            case 6: // SEI帧
                std::cout << "SEI frame" << std::endl;
                break;
            case 7: // SPS
                sps = nalu;
                _sps = nalu;
                break;
            case 8: // PPS
                pps = nalu;
                _pps = nalu;
                break;
            default:
                break;
        }
    }

    // 首次处理，必须等到IDR帧，否则画面黑屏
    if (!_is_first_idr_sent) {
        if (!idr) {
            std::cerr << "Waiting for first IDR frame, dropping current frame" << std::endl;
            return;
        }
        std::cout << "First IDR frame received, Starting stream" << std::endl;

        // 构建首帧：SPS + PPS + IDR
        std::vector<uint8_t> pes_payload;
        if (!_sps.empty() && !_pps.empty()) {
            print_sps_pps(_sps, _pps);
            HeaderBuilder::appendStartCode(pes_payload, _sps.data(), _sps.size());
            HeaderBuilder::appendStartCode(pes_payload, _pps.data(), _pps.size());
        } else if (!sps.empty() && !pps.empty()) {
            print_sps_pps(sps, pps);
            HeaderBuilder::appendStartCode(pes_payload, sps.data(), sps.size());
            HeaderBuilder::appendStartCode(pes_payload, pps.data(), pps.size());
        } else {
            std::cerr << "No SPS/PPS found for first IDR" << std::endl;
            return;
        }

        // 写入IDR
        HeaderBuilder::appendStartCode(pes_payload, idr->data(), idr->size());

        // 构建PES包
        buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, true);
        _is_first_idr_sent = true;
        return;
    }

    // 后续帧处理
    for (const auto &nalu: nalUnits) {
        if (nalu.empty()) continue;
        const uint8_t nal_type = nalu[0] & 0x1F;
        if (nal_type == 5) {
            // IDR帧
            std::vector<uint8_t> pes_payload;
            if (!_sps.empty() && !_pps.empty()) {
                HeaderBuilder::appendStartCode(pes_payload, _sps.data(), _sps.size());
                HeaderBuilder::appendStartCode(pes_payload, _pps.data(), _pps.size());
            } else if (!sps.empty() && !pps.empty()) {
                HeaderBuilder::appendStartCode(pes_payload, sps.data(), sps.size());
                HeaderBuilder::appendStartCode(pes_payload, pps.data(), pps.size());
            } else {
                std::cerr << "No SPS/PPS found in current frame, dropping frame" << std::endl;
                continue;
            }
            HeaderBuilder::appendStartCode(pes_payload, nalu.data(), nalu.size());
            buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, true);
        } else if (nal_type == 6) {
            std::cout << "SEI frame" << std::endl;
        } else if (nal_type == 7 || nal_type == 8) {
            if (nal_type == 7) {
                _sps = nalu;
            }
            if (nal_type == 8) {
                _pps = nalu;
            }
        } else {
            // P帧
            std::vector<uint8_t> pes_payload;
            HeaderBuilder::appendStartCode(pes_payload, nalu.data(), nalu.size());
            buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, false);
        }
    }
}

/**
 * 音频帧：G.711μ
 * */
void PsMuxer::writeAudioFrame(const uint8_t *pcm_data, const uint64_t pts_90k, const int size) {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    if (!_is_first_idr_sent) {
        std::cerr << "audio wait sending IDR frame" << std::endl;
        return;
    }

    // 计算样本数
    const size_t samples = size / sizeof(uint8_t);
    std::vector<int16_t> pcm_buffer(samples);

    // 将8位PCM数据转换为16位PCM数据
    for (int i = 0; i < samples; ++i) {
        pcm_buffer[i] = static_cast<int16_t>((pcm_data[i] - 128) << 8);
    }

    // 输出缓冲区大小应与输入样本数匹配
    std::vector<uint8_t> mulaw_buffer(samples * sizeof(int16_t));
    PcmEncoder::encode_to_mulaw(pcm_buffer.data(), mulaw_buffer.data(), samples);

    // 封装 PES 包
    buildPesPacket(AUDIO_STREAM_ID, mulaw_buffer.data(), size, pts_90k, false);
}

void PsMuxer::release() {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    _sps.clear();
    _pps.clear();
    _is_first_idr_sent = false;

    std::cout << "PsMuxer released" << std::endl;
}
