//
// Created by peng on 2026/1/4.
//

#include "ps_muxer.hpp"
#include "base_config.hpp"
#include "header_builder.hpp"
#include "audio_processor.hpp"
#include "rtp_sender.hpp"
#include "utils.hpp"

#include <iostream>

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
 * @param payload
 * @param len
 * @param pts
 * @param is_key_frame
 * */
static void buildPsPacket(const uint8_t* payload, const size_t len, const uint64_t pts, const bool is_key_frame) {
    // ================================ 添加PS头 ================================//
    const auto ps_header = HeaderBuilder::buildPsPackHeader(pts);

    // ================================ 添加系统头和PSM ================================//
    std::vector<uint8_t> config{};
    if (is_key_frame) {
        config = HeaderBuilder::buildSystemHeader(VIDEO_STREAM_ID,AUDIO_STREAM_ID);
        auto psm = HeaderBuilder::buildPsMap();
        config.insert(config.end(), psm.begin(), psm.end());
    }

    // ================================ 封装 PS 包 ================================//
    // 计算总大小
    const size_t total_size = ps_header.size() + config.size() + len;
    std::vector<uint8_t> ps_pkt(total_size);

    // 添加 PS 头
    size_t offset = 0;
    std::memcpy(ps_pkt.data() + offset, ps_header.data(), ps_header.size());
    offset += ps_header.size();

    // 添加 System Header 和 PSM（如果存在）
    if (!config.empty()) {
        std::memcpy(ps_pkt.data() + offset, config.data(), config.size());
        offset += config.size();
    }

    // 添加 PES 载荷数据
    std::memcpy(ps_pkt.data() + offset, payload, len);

    // 发送PS包。关键帧（I帧）通常是一组帧序列的最后一帧或开始帧
    RtpSender::get()->sendDataPacket(ps_pkt.data(), ps_pkt.size(), is_key_frame, pts);
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
static void buildPesPacket(const uint8_t stream_id, const uint8_t* payload, const size_t len, const uint64_t pts_90k,
                           const bool is_key_frame) {
    std::vector<uint8_t> pes_header = HeaderBuilder::buildPesHeader(stream_id, len, pts_90k);

    // ================================ 封装 PES 包 ================================//
    // 不必管是SPS/PPS/G.711μ/IDR/P，这些都是PES的载荷，载荷位置 = 动态的，由第9字节决定。
    std::vector<uint8_t> pes_pkt(pes_header.size() + len); // pes_header + 负载数据

    // 添加 PES header
    std::copy(pes_header.begin(), pes_header.end(), pes_pkt.begin());

    // 添加载荷数据
    std::memcpy(pes_pkt.data() + pes_header.size(), payload, len);

    // 封装PS包
    buildPsPacket(pes_pkt.data(), pes_pkt.size(), pts_90k, is_key_frame);
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

    const auto nalUnits = Utils::splitNalUnits(h264_data, size);
    if (!Utils::validateNalUnits(nalUnits)) {
        return;
    }

    bool has_idr_frame = false;
    std::vector<uint8_t> sps{};
    std::vector<uint8_t> pps{};
    // 通常情况单个关键帧只包含一个IDR帧，但是为了代码健壮性，用数组保存更稳妥，并且在同一次函数调用中就被立即使用，不需要显式清空 idr_frames
    std::vector<std::vector<uint8_t>> idr_frames{};

    // 预分配内存，避免频繁的内存分配
    idr_frames.reserve(2); // 通常关键帧包含1-2个IDR帧

    // 解析NALUs并识别SPS/PPS/IDR
    for (const auto& nalu : nalUnits) {
        if (nalu.empty())
            continue;
        const uint8_t nalType = nalu[0] & 0x1F;
        switch (nalType) {
            case 5: // IDR帧
                has_idr_frame = true;
                idr_frames.emplace_back(nalu); // 使用emplace_back避免拷贝
                break;
            case 6: // SEI
                break;
            case 7: // SPS
                sps = nalu;
                _sps_cache = nalu;
                break;
            case 8: // PPS
                pps = nalu;
                _pps_cache = nalu;
                break;
            default:
                break;
        }
    }

    // 等待接收到第一个IDR帧才开始处理
    if (_is_waiting_for_idr) {
        if (!has_idr_frame) {
            return;
        }
        _is_waiting_for_idr = false;
        std::cout << "[VIDEO] First IDR received, stream started" << std::endl;
    }

    // 如果是关键帧，先打包 SPS+PPS+IDR
    if (has_idr_frame) {
        // 总是使用当前帧中的SPS/PPS，如果存在则更新缓存
        if (!sps.empty() && !pps.empty()) {
            // 使用move避免拷贝
            _sps_cache = std::move(sps);
            _pps_cache = std::move(pps);
        }

        // 检查SPS/PPS缓存是否有效
        if (_sps_cache.empty() || _pps_cache.empty()) {
            std::cerr << "[VIDEO] No SPS/PPS available, dropping key frame" << std::endl;
            return;
        }

        // 预分配PES载荷空间
        std::vector<uint8_t> pes_payload;

        // 直接构建SPS+PPS数据，避免中间向量
        HeaderBuilder::insertStartCode(pes_payload, _sps_cache.data(), _sps_cache.size());
        HeaderBuilder::insertStartCode(pes_payload, _pps_cache.data(), _pps_cache.size());

        // 直接添加IDR数据到同一个向量，避免额外的拷贝
        for (const auto& idr_nalu : idr_frames) {
            HeaderBuilder::insertStartCode(pes_payload, idr_nalu.data(), idr_nalu.size());
        }

        // 封装IDR帧为PES包（标记为关键帧）
        if (!pes_payload.empty()) {
            buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, true);
            _is_idr_sent = true;
        }
    } else {
        // 处理非IDR帧（P/B帧）
        std::vector<uint8_t> pes_payload;

        // 预分配空间，避免多次重新分配
        size_t estimated_size = 0;
        for (const auto& nalu : nalUnits) {
            if (!nalu.empty()) {
                const uint8_t nal_type = nalu[0] & 0x1F;
                if (nal_type != 7 && nal_type != 8) {
                    estimated_size += nalu.size() + 4; // 起始码大小
                }
            }
        }

        if (estimated_size > 0) {
            pes_payload.reserve(estimated_size);
        }

        // 构建PES载荷
        for (const auto& nalu : nalUnits) {
            if (nalu.empty())
                continue;
            const uint8_t nal_type = nalu[0] & 0x1F;
            // 跳过SPS/PPS，只处理帧数据
            if (nal_type != 7 && nal_type != 8) {
                HeaderBuilder::insertStartCode(pes_payload, nalu.data(), nalu.size());
            }
        }

        // 封装非关键帧
        if (!pes_payload.empty()) {
            buildPesPacket(VIDEO_STREAM_ID, pes_payload.data(), pes_payload.size(), pts_90k, false);
        } else {
            std::cerr << "[VIDEO] No valid frame data found in non-IDR frame" << std::endl;
        }
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