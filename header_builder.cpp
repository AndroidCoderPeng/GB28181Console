//
// Created by peng on 2025/12/21.
//

#include "header_builder.hpp"
#include "base_config.hpp"
#include "utils.hpp"

void HeaderBuilder::appendStartCode(std::vector<uint8_t> &dst, const uint8_t *data, const size_t len) {
    static constexpr uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    dst.insert(dst.end(), start_code, start_code + 4);
    dst.insert(dst.end(), data, data + len);
}

std::vector<uint8_t> HeaderBuilder::buildSystemHeader() {
    static const uint8_t system_header[] = {
        0x00, 0x00, 0x01, 0xBB, // PS流系统头，固定值
        0x00, 0x0C, // 00 0C：‘系统头’后续长度=12字节
        0x80, // 1 0 0 0 0 0 0 0，系统头标志，第1位marker_bit=1，按规范固定
        0x04, 0xFF, 0xFF, // 最大码率（0x04FFFF），主流GB推流模板，单位50字节/秒
        0xE0, // 实际用不到，写模板值
        0x07, // 保留位，主流写0x07
        0xC0, // 保留位，主流写0xC0
        0x0F, // 模板值0x0F，主流用途
        VIDEO_STREAM_ID, // 视频stream_id（首路视频）
        0x07, 0x00, // 视频buffer bound（0x0700，通常取7K）
        AUDIO_STREAM_ID, // 音频stream_id（首路音频）
        0x07, 0x00 // 音频buffer bound（0x0700，通常取7K）
    };
    return {system_header, system_header + sizeof(system_header)};
}

std::vector<uint8_t> HeaderBuilder::buildPsm() {
    std::vector<uint8_t> psm = {
        // Header
        0x00, 0x00, 0x01, 0xBC, // PS map header
        0x00, 0x1A, // program_stream_map_length (26 bytes)，没有特殊情况PSM长度固定就是26字节，除非有多路音频等信息
        0xE0, 0xFF, // current/next/version，没特殊的版本管理需求，可以固定写 0xE0 0xFF
        0x00, 0x00, // program_stream_info_length (0)
        0x00, 0x14, // elementary_stream_map_length (20 bytes)，没有特殊情况ESM长度固定就是20字节，除非有多路音视频等信息

        // Elementary Streams:
        // 1. H.264 video
        VIDEO_STREAM_TYPE, VIDEO_STREAM_ID, 0x00, 0x00, // info_length(0)

        // 2. G.711μ audio
        AUDIO_STREAM_TYPE, AUDIO_STREAM_ID, 0x00, 0x00, // info_length(0)

        // 填充到20字节 elementary_stream_map (共8字节填充）
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // CRC32校验【4字节】
    const uint32_t crc = Utils::mpeg2Crc32(&psm[6], 26 - 4);
    psm.push_back(crc >> 24 & 0xFF);
    psm.push_back(crc >> 16 & 0xFF);
    psm.push_back(crc >> 8 & 0xFF);
    psm.push_back(crc & 0xFF);

    return psm;
}

std::vector<uint8_t> HeaderBuilder::buildPsPackHeader(const uint64_t pts_90k) {
    std::vector<uint8_t> ps_header(14);

    // 起始码【4字节】
    ps_header[0] = 0x00;
    ps_header[1] = 0x00;
    ps_header[2] = 0x01;
    ps_header[3] = 0xBA;

    // SCR【6字节】
    const uint64_t scr = pts_90k & 0x1FFFFFFFFULL; // 强制保留低 33 位
    ps_header[4] = 0x44 | ((scr >> 30) & 0x07);
    ps_header[5] = (scr >> 22) & 0xFF;
    ps_header[6] = (scr >> 14) & 0xFF;
    ps_header[7] = 0x01 | ((scr >> 6) & 0xFE);
    ps_header[8] = (scr << 2) & 0xFF;
    ps_header[9] = 0x01;

    // 码流率，直接用最大值模板，提高兼容性
    ps_header[10] = 0xFF;
    ps_header[11] = 0xFF;
    ps_header[12] = 0xFC;
    ps_header[13] = 0x00;
    return ps_header;
}
