//
// Created by peng on 2026/1/4.
//

#include "ps_muxer.hpp"
#include "audio_processor.hpp"
#include "base_config.hpp"
#include "h264_splitter.hpp"
#include "header_builder.hpp"
#include "rtp_sender.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>

// GB28181 标准：每个 PS 包的最大大小（考虑 MTU 1500，减去 IP/UDP/RTP 头部）
// PS包 = PS Header(14) + System Header(~18) + PSM(~24) + PES Header(~19) + Payload
// 为了确保不超过 MTU，PES 负载应控制在 1300 字节以内
static constexpr size_t MAX_PES_PAYLOAD_PER_PACKET = 1300;

static void print(const std::string& tag, const std::vector<uint8_t>& data) {
    std::string str;
    for (const unsigned char s : data) {
        char hex_byte[8];
        snprintf(hex_byte, sizeof(hex_byte), "%02X ", s);
        str += hex_byte;
    }

    std::cout << "size: " << data.size() << ", " << tag << ": " << str << std::endl;
}

/**
 * 封装 PS 包，其实就是把 PES 包转为 PS 包，通俗来说就是将音视频同一化，方便后续封装为PS流
 *
 * PS 包 = [起始码] + [PS Header] + [System Header(仅IDR帧)] + [PSM(仅IDR帧)] + [PES 包]
 *
 * @param payload PES 包数据
 * @param len PES 包大小
 * @param pts_90k 时间戳（90kHz）
 * @param is_key_frame 是否为关键帧
 * */
static void buildPsPacket(const uint8_t* payload, const size_t len, const uint64_t pts_90k, const bool is_key_frame) {
    // ================================ 添加PS头 ================================//
    const auto ps_header = HeaderBuilder::buildPsPackHeader(pts_90k);

    // ================================ 添加系统头和PSM ================================//
    std::vector<uint8_t> config{};
    if (is_key_frame) {
        config = HeaderBuilder::buildSystemHeader(VIDEO_STREAM_ID, AUDIO_STREAM_ID);
        auto psm = HeaderBuilder::buildPsMap();
        config.insert(config.end(), psm.begin(), psm.end());
    }

    // ================================ 封装 PS 包 ================================//
    // 计算总大小
    const size_t total_size = ps_header.size() + config.size() + len;
    std::vector<uint8_t> ps_pkt(total_size);

    size_t offset = 0;
    memcpy(ps_pkt.data() + offset, ps_header.data(), ps_header.size());
    offset += ps_header.size();

    // 添加 System Header 和 PSM（如果存在）
    if (!config.empty()) {
        memcpy(ps_pkt.data() + offset, config.data(), config.size());
        offset += config.size();
    }

    // 添加 PES 载荷数据
    memcpy(ps_pkt.data() + offset, payload, len);

    RtpSender::get()->sendDataPacket(ps_pkt.data(), ps_pkt.size(), is_key_frame, pts_90k);
}

/**
 * 封装 PES 包，其实就是把 整帧的 NALU 或者 G.711μ 数据转为 PES 包，通俗来说就是将音视频同一化，方便后续封装为PS流
 *
 * PES包 = [起始码] + [PES Header] + [负载数据]
 *
 * @param stream_id 流ID
 * @param payload 负载数据（没有起始码）
 * @param len 负载大小
 * @param pts_90k 时间戳（90kHz）
 * @param is_key_frame 是否为关键帧
 * */
static void buildPesPacket(const uint8_t stream_id, const uint8_t* payload, size_t len, const uint64_t pts_90k,
                           const bool is_key_frame) {
    // 如果负载小于阈值，直接封装成一个 PS 包
    if (len <= MAX_PES_PAYLOAD_PER_PACKET) {
        std::vector<uint8_t> pes_header = HeaderBuilder::buildPesHeader(stream_id, len, pts_90k);

        // 不必管是SPS/PPS/G.711μ/IDR/P，这些都是PES的载荷，载荷位置 = 动态的，由第9字节决定。
        std::vector<uint8_t> pes_pkt(pes_header.size() + len); // pes_header + 负载数据

        // 添加 PES header
        std::copy(pes_header.begin(), pes_header.end(), pes_pkt.begin());

        // 添加载荷数据
        std::memcpy(pes_pkt.data() + pes_header.size(), payload, len);

        // 封装PS包
        buildPsPacket(pes_pkt.data(), pes_pkt.size(), pts_90k, is_key_frame);
    } else {
        size_t remaining = len;
        size_t offset = 0;
        int packet_index = 0;

        while (remaining > 0) {
            size_t chunk_size = (remaining > MAX_PES_PAYLOAD_PER_PACKET) ? MAX_PES_PAYLOAD_PER_PACKET : remaining;

            // 为每个分片构建PES包
            std::vector<uint8_t> pes_header = HeaderBuilder::buildPesHeader(stream_id, chunk_size, pts_90k);
            std::vector<uint8_t> pes_pkt(pes_header.size() + chunk_size);

            std::copy(pes_header.begin(), pes_header.end(), pes_pkt.begin());
            std::memcpy(pes_pkt.data() + pes_header.size(), payload + offset, chunk_size);

            // 只有最后一个包标记 marker bit（关键帧标记）
            const bool mark_as_key = is_key_frame && (remaining <= MAX_PES_PAYLOAD_PER_PACKET);

            buildPsPacket(pes_pkt.data(), pes_pkt.size(), pts_90k, mark_as_key);

            std::cout << "PES分片[" << packet_index << "]: offset=" << offset <<
                    ", size=" << chunk_size << ", remaining=" << remaining << std::endl;

            offset += chunk_size;
            remaining -= chunk_size;
            packet_index++;
        }
        std::cout << "PES分片完成: 总大小=" << len << ", 分片数=" << packet_index << std::endl;
    }
}

/**
 * 视频帧：H.264，时间基已经设置为90kHz
 * - 一个 cv::Mat 经过编码后可能包含多种NALU类型，但是至多只有一个IDR
 * - 一个帧可能包含多个 NALU
 *
 * <p>
 * NALU 类型与帧的关系
 * - IDR帧：由一个IDR类型的NALU构成
 * - P帧：由一个或多个P slice类型的NALU构成
 * */
void PsMuxer::writeVideoFrame(const uint8_t* h264_data, const uint64_t pts_90k, const size_t size) {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    std::vector<NALU> nalu_vector{};
    const int nalu_count = H264Splitter::splitH264Frame(h264_data, size, nalu_vector);
    if (nalu_count == 0) {
        std::cerr << "H.264 frame is empty" << std::endl;
        return;
    }

    std::vector<NALU> other_frames{};
    std::vector<NALU> idr_frames{};
    const NALU* sps_ptr = nullptr;
    const NALU* pps_ptr = nullptr;

    // 解析NALUs并识别SPS/PPS/IDR
    for (auto& nalu : nalu_vector) {
        if (!nalu.data)
            continue;
        switch (nalu.type) {
            case 1: // 非IDR帧（P帧）
                other_frames.push_back(nalu);
                break;
            case 5: // IDR帧
                idr_frames.push_back(nalu);
                break;
            case 6: // SEI
                break;
            case 7: // SPS
                sps_ptr = &nalu;
                _sps_cache.assign(nalu.data, nalu.data + nalu.size);
                std::cout << "保存SPS，大小=" << _sps_cache.size() << ", 前4字节: "
                        << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                        << static_cast<int>(_sps_cache[0]) << " "
                        << static_cast<int>(_sps_cache[1]) << " "
                        << static_cast<int>(_sps_cache[2]) << " "
                        << static_cast<int>(_sps_cache[3]) << std::dec << std::endl;
                break;
            case 8: // PPS
                pps_ptr = &nalu;
                _pps_cache.assign(nalu.data, nalu.data + nalu.size);
                std::cout << "保存PPS，大小=" << _pps_cache.size() << std::endl;
                break;
            default:
                break;
        }
    }

    // 等待接收到第一个IDR帧才开始处理
    if (_is_waiting_for_idr) {
        if (idr_frames.empty()) {
            std::cout << "[VIDEO] Waiting for first IDR frame, dropping current frame" << std::endl;
            return;
        }
        _is_waiting_for_idr = false;
        std::cout << "[VIDEO] First IDR received, stream started" << std::endl;
    }

    // 如果是关键帧，先打包 SPS+PPS+IDR
    if (!idr_frames.empty()) {
        std::cout << "┌─────────────────────────────────────┐" << std::endl;
        std::cout << "处理IDR帧，共 " << idr_frames.size() << " 个" << std::endl;

        std::vector<uint8_t> pes_payload;

        // 优先使用当前帧的SPS/PPS，否则使用缓存
        const uint8_t* sps_data = nullptr;
        size_t sps_size = 0;
        const uint8_t* pps_data = nullptr;
        size_t pps_size = 0;

        if (sps_ptr) {
            sps_data = sps_ptr->data;
            sps_size = sps_ptr->size;
        } else if (!_sps_cache.empty()) {
            sps_data = _sps_cache.data();
            sps_size = _sps_cache.size();
        }

        if (pps_ptr) {
            pps_data = pps_ptr->data;
            pps_size = pps_ptr->size;
        } else if (!_pps_cache.empty()) {
            pps_data = _pps_cache.data();
            pps_size = _pps_cache.size();
        }

        if (!sps_data || !pps_data) {
            std::cerr << "No SPS/PPS available, dropping IDR frame" << std::endl;
            return;
        }

        // 添加 SPS（带起始码）
        pes_payload.insert(pes_payload.end(), {0x00, 0x00, 0x00, 0x01});
        pes_payload.insert(pes_payload.end(), sps_data, sps_data + sps_size);
        std::cout << "添加SPS后 pes_payload 大小=" << pes_payload.size() << std::endl;

        // 添加 PPS（带起始码）
        pes_payload.insert(pes_payload.end(), {0x00, 0x00, 0x00, 0x01});
        pes_payload.insert(pes_payload.end(), pps_data, pps_data + pps_size);
        std::cout << "添加PPS后 pes_payload 大小=" << pes_payload.size() << std::endl;

        // 添加所有IDR帧（带起始码）
        for (const auto& idr : idr_frames) {
            pes_payload.insert(pes_payload.end(), {0x00, 0x00, 0x00, 0x01});
            pes_payload.insert(pes_payload.end(), idr.data, idr.data + idr.size);
            std::cout << "添加IDR后 pes_payload 大小=" << pes_payload.size() << std::endl;
        }

        // 打印最终的PES payload前64字节
        std::string payload_str;
        size_t print_len = pes_payload.size() < 64 ? pes_payload.size() : 64;
        payload_str.reserve(print_len * 3);
        for (size_t i = 0; i < print_len; i++) {
            char hex_byte[4];
            snprintf(hex_byte, sizeof(hex_byte), "%02X ", pes_payload[i]);
            payload_str += hex_byte;
        }
        std::cout << "最终PES payload前" << print_len << "字节: " << payload_str.c_str() << std::endl;
        std::cout << "└─────────────────────────────────────┘" << std::endl;

        // 封装IDR帧为PES包（标记为关键帧）
        buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, true);
        _is_idr_sent = true;
    } else if (!other_frames.empty()) {
        // 处理非IDR帧（P/B帧）
        std::vector<uint8_t> pes_payload;

        for (const auto& nalu : other_frames) {
            // 添加起始码 + NALU payload
            pes_payload.insert(pes_payload.end(), {0x00, 0x00, 0x00, 0x01});
            pes_payload.insert(pes_payload.end(), nalu.data, nalu.data + nalu.size);
        }

        if (!pes_payload.empty()) {
            // 封装非关键帧为PES包
            buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, false);
        }
    } else {
        std::cerr << "没有IDR帧也没有P帧" << std::endl;
    }
}

void PsMuxer::writeAudioFrame(const uint8_t* pcm_data, const uint64_t pts_90k, const size_t len) {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    if (!_is_idr_sent) {
        std::cerr << "[AUDIO] Waiting for first video IDR" << std::endl;
        return;
    }

    // 计算样本数
    const size_t samples = len / sizeof(uint8_t);
    std::vector<int16_t> pcm_buffer(samples);

    // 将8位PCM数据转换为16位PCM数据
    for (int i = 0; i < samples; ++i) {
        pcm_buffer[i] = static_cast<int16_t>((pcm_data[i] - 128) << 8);
    }

    // 输出缓冲区大小应与输入样本数匹配
    std::vector<uint8_t> g711_buffer(samples * sizeof(int16_t));
    AudioProcessor::pcm_to_ulaw(pcm_buffer.data(), g711_buffer.data(), samples);
    // AudioProcessor::pcm_to_alaw(pcm_buffer.data(), g711_buffer.data(), samples);

    // 封装 PES 包
    buildPesPacket(AUDIO_STREAM_ID, g711_buffer.data(), g711_buffer.size(), pts_90k, false);
}

void PsMuxer::release() {
    std::lock_guard<std::mutex> lock(_muxer_mutex);

    _sps_cache.clear();
    _pps_cache.clear();
    _is_waiting_for_idr = true;
    _is_idr_sent = false;

    std::cout << "PsMuxer released" << std::endl;
}