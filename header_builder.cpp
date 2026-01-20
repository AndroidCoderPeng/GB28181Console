//
// Created by peng on 2025/12/21.
//

#include "header_builder.hpp"
#include "base_config.hpp"
#include "utils.hpp"

#include <iostream>

/**
 * 给 SPS/PPS/IDR/P 添加起始码
 * @param dst
 * @param data
 * @param len 帧长度
 */
void HeaderBuilder::insertStartCode(std::vector<uint8_t>& dst, const uint8_t* data, const size_t len)
{
    if (data == nullptr)
    {
        std::cerr << "Error: data pointer is null in insertStartCode" << std::endl;
        return;
    }

    if (len == 0)
    {
        std::cerr << "Warning: len is 0 in insertStartCode" << std::endl;
        return;
    }

    static constexpr uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    dst.insert(dst.end(), start_code, start_code + 4);
    dst.insert(dst.end(), data, data + len);

    // 打印插入后 dst 的前几字节
    // std::cout << " 添加起始码后dst前24字节: ";
    // for (size_t i = 0; i < std::min(dst.size(), static_cast<size_t>(24)); ++i) {
    //     printf("%02X ", dst[i]);
    // }
    // std::cout << std::endl;
}

/**
 * 两个概念：
 * PES 和 NALU
 *
 * - NALU：网络应用层单元，即视频帧，是H.264数据的基本单元
 * - PES 包：包含负载数据，以及PTS/DTS等信息，是PS流中的基本单元。视频帧较长需要分割为NALU，音频帧（160字节不用分割），再封装成PES包
 * - 封装 PES 包，其实就是把 整帧的 NALU 或者 G.711μ 数据转为 PES 包，通俗来说就是将音视频同一化，方便后续封装为PS流
 *
 * PES包 = [起始码] + [媒体ID] + [PES包长度] + [媒体信息(可选字段)] + [负载数据]
 * */
std::vector<uint8_t> HeaderBuilder::buildPesHeader(const uint8_t stream_id, const size_t len, const uint64_t pts_90k,
                                                   const bool is_key_frame)
{
    // PES 长度计算：PES头可选字段 + 负载数据长度
    const uint32_t payload_len = 8 + len;

    // 判断是否超过 uint16_t 最大值
    const bool use_undefined_length = (payload_len > 0xFFFF);
    const uint16_t pes_len_field = use_undefined_length ? 0 : static_cast<uint16_t>(payload_len);

    // 标准 PES 头大小固定为 14 字节
    std::vector<uint8_t> pes_header(14);

    // PES start code【3字节】
    pes_header[0] = 0x00;
    pes_header[1] = 0x00;
    pes_header[2] = 0x01;

    // 流ID【1字节】
    pes_header[3] = stream_id;

    // PES 长度【2字节】
    pes_header[4] = (pes_len_field >> 8) & 0xFF;
    pes_header[5] = pes_len_field & 0xFF;

    // ================================ PES 可选字段 ================================//

    // PES 包第一标志【1字节】
    if (is_key_frame)
    {
        // 1000 0111 -> 87
        pes_header[6] = 0x87;
    }
    else
    {
        // 1000 0011 -> 83
        pes_header[6] = 0x83;
    }

    // PES 包第二标志【1字节】
    pes_header[7] = 0x80; // 只包含 PTS

    // PES 头附加信息长度【1字节】
    pes_header[8] = 0x05; // PTS 占用 5 字节

    // PTS【5字节】
    const uint64_t pts = pts_90k & 0x1FFFFFFFFULL;
    pes_header[9] = 0x20 | ((pts >> 29) & 0x0E) | 0x01; // byte[9]: [0010] + [bits 32-30] + [marker=1]
    pes_header[10] = (pts >> 22) & 0xFF;                // byte[10]: bits 29-22
    pes_header[11] = ((pts >> 14) & 0xFE) | 0x01;       // byte[11]: [bits 21-15] + [marker=1]
    pes_header[12] = (pts >> 7) & 0xFF;                 // byte[12]: bits 14-7
    pes_header[13] = ((pts << 1) & 0xFE) | 0x01;        // byte[13]: [bits 6-0] + [marker=1]
    return pes_header;
}

std::vector<uint8_t> HeaderBuilder::buildSystemHeader(const uint8_t video_stream_id, const uint8_t audio_stream_id)
{
    static const uint8_t system_header[] = {
        0x00, 0x00, 0x01,       // 起始码，固定值
        0xBB,                   // PS流ID，固定值
        0x00, 0x0C,             // 00 0C：‘系统头’后续长度=12字节
        0x80,                   // 1000 0000，系统头标志，第1位marker_bit=1，按规范固定
        0x04, 0xFF, 0xFF,       // 最大码率（0x04FFFF），主流GB推流模板，单位50字节/秒
        0xE0, 0x07, 0xC0, 0x0F, // 预留字段
        video_stream_id,        // 视频stream_id（首路视频）
        0x20, 0x00,             // 视频buffer bound（0x2000，约1MB）
        audio_stream_id,        // 音频stream_id（首路音频）
        0x01, 0x00              // 音频buffer bound（0x0100，约32KB）
    };
    return {system_header, system_header + sizeof(system_header)};
}

std::vector<uint8_t> HeaderBuilder::buildPsMap()
{
    std::vector<uint8_t> psm;

    // 起始码和 PS Map ID
    psm.push_back(0x00);
    psm.push_back(0x00);
    psm.push_back(0x01);
    psm.push_back(0xBC);

    // PSM 长度占位符 (稍后填充)
    size_t length_pos = psm.size();
    psm.push_back(0x00);
    psm.push_back(0x00);

    // 当前有效标志、版本号、保留位
    psm.push_back(0xE0); // 当前有效(1) + 版本0(00000) + 保留(11)
    psm.push_back(0xFF); // 保留(1111111) + 标记位(1)

    // Program Stream Info Length
    psm.push_back(0x00);
    psm.push_back(0x00);

    // Elementary Stream Map Length (每个流占4字节)
    constexpr uint16_t es_map_length = 8; // 2个流 × 4字节
    psm.push_back((es_map_length >> 8) & 0xFF);
    psm.push_back(es_map_length & 0xFF);

    // 视频流信息
    psm.push_back(STREAM_TYPE_H264);
    psm.push_back(VIDEO_STREAM_ID);
    psm.push_back(0x00); // ES Info Length (高字节)
    psm.push_back(0x00); // ES Info Length (低字节)

    // 音频流信息
    psm.push_back(STREAM_TYPE_G711);
    psm.push_back(AUDIO_STREAM_ID);
    psm.push_back(0x00); // ES Info Length (高字节)
    psm.push_back(0x00); // ES Info Length (低字节)

    // CRC32 占位符
    const size_t crc_pos = psm.size();
    psm.push_back(0x00);
    psm.push_back(0x00);
    psm.push_back(0x00);
    psm.push_back(0x00);

    // 计算并填充 PSM 长度 (从长度字段之后到PSM结束的字节数)
    const uint16_t psm_length = psm.size() - length_pos - 2;
    psm[length_pos] = (psm_length >> 8) & 0xFF;
    psm[length_pos + 1] = psm_length & 0xFF;

    // CRC32校验【4字节】(从 PS Map ID 之后到 CRC 之前的所有字节)
    const uint32_t crc = Utils::calculateCRC32(psm, 4, crc_pos - 4);
    psm[crc_pos] = (crc >> 24) & 0xFF;
    psm[crc_pos + 1] = (crc >> 16) & 0xFF;
    psm[crc_pos + 2] = (crc >> 8) & 0xFF;
    psm[crc_pos + 3] = crc & 0xFF;

    return psm;
}

std::vector<uint8_t> HeaderBuilder::buildPsPackHeader(const uint64_t pts_90k)
{
    std::vector<uint8_t> ps_header(14);

    // 起始码【3字节】
    ps_header[0] = 0x00;
    ps_header[1] = 0x00;
    ps_header[2] = 0x01;

    // PS包头【1字节】
    ps_header[3] = 0xBA;

    // SCR【6字节 = 48位】——33位数据，其他位是标志位，90kHz时钟
    const auto scr = pts_90k & 0x1FFFFFFFFULL; // 强制保留低 33 位
    constexpr auto scr_ext = 0;                // SCR extension，通常为 0
    ps_header[4] = 0x40                        // '0100 0000' 起始标记
        | ((scr >> 27) & 0x38)                 // scr[32:30] 移到 bit[5: 3]
        | 0x04                                 // marker_bit = 1 在 bit[2]
        | ((scr >> 28) & 0x03);                // scr[29:28] 移到 bit[1:0]
    ps_header[5] = (scr >> 20) & 0xFF;
    ps_header[6] = ((scr >> 12) & 0xF8) // scr[19:15] 移到 bit[7:3]
        | 0x04                          // marker_bit = 1 在 bit[2]
        | ((scr >> 13) & 0x03);         // scr[14:13] 移到 bit[1:0]
    ps_header[7] = (scr >> 5) & 0xFF;
    ps_header[8] = ((scr << 3) & 0xF8)     // scr[4:0] 移到 bit[7:3]
        | 0x04                             // marker_bit = 1 在 bit[2]
        | ((scr_ext >> 7) & 0x03);         // scr_ext[8:7] 移到 bit[1:0]
    ps_header[9] = ((scr_ext << 1) & 0xFE) // scr_ext[6:0] 移到 bit[7:1]
        | 0x01;                            // marker_bit = 1 在 bit[0]

    // 码流率，直接用最大值模板，提高兼容性
    ps_header[10] = 0xFF;
    ps_header[11] = 0xFF;
    ps_header[12] = 0xFC;
    ps_header[13] = 0x00;
    return ps_header;
}
