//
// Created by peng on 2025/12/21.
//

#include "header_builder.hpp"
#include "base_config.hpp"
#include "utils.hpp"

#include <iostream>

std::vector<uint8_t> HeaderBuilder::buildPesHeader(const uint8_t stream_id, const size_t len, const uint64_t pts_90k) {
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
    pes_header[4] = (pes_len >> 8) & 0xFF;
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
    // 1000 0111 -> 0x87: MPEG-2 + 未加扰 + 数据对齐 + 有版权 + 原始流
    pes_header[6] = 0x87;

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
    // 1000 0000 -> 0x80
    pes_header[7] = 0x80;

    // PES头的附加信息，从第10个字节开始（不包括前9个字节和自己），往后数多少个字节
    pes_header[8] = 0x05;

    // PTS【5字节】，所以附加信息长度就是5。MPEG-2 PTS 只有 33 位，必须 mask 掉高位，避免溢出
    /**
     * 00000000 00000000 00000000 00000000 00000000
     * [0010][32..30][1][29..15][1][14..0][1]
     */
    const auto pts = pts_90k & 0x1FFFFFFFFULL;
    pes_header[9] = 0x20 | ((pts >> 30) & 0x07) << 1; // 获取第32-29位, 注意要左移一位加上marker_bit逻辑
    pes_header[9] |= 0x20;                            // 加上marker_bit

    pes_header[10] = (pts >> 22) & 0xFF; // 获取第28-21位
    pes_header[10] |= 0x01 << 7;         // 加上marker_bit

    pes_header[11] = (pts >> 14) & 0xFE; // 获取第20-14位, 注意第一位之后需要加marker_bit
    pes_header[11] |= 0x01;              // 加上marker_bit

    pes_header[12] = (pts >> 7) & 0xFF; // 获取第13-7位
    pes_header[12] |= 0x01 << 7;        // 加上marker_bit

    pes_header[13] = (pts & 0xFE) | 0x01; // 获取第6-0位, 并在最后加上marker_bit
    return pes_header;
}

std::vector<uint8_t> HeaderBuilder::buildSystemHeader(const uint8_t video_stream_id,
                                                      const uint8_t audio_stream_id) {
    static const uint8_t system_header[] = {
        0x00, 0x00, 0x01,       // 起始码，固定值
        0xBB,                   // PS流ID，固定值
        0x00, 0x10,             // 00 10：'系统头'后续长度=16字节
        0x80,                   // 1000 0000，系统头标志，第1位marker_bit=1，按规范固定
        0x04, 0xFF, 0xFF,       // 最大码率（0x04FFFF），主流GB推流模板，单位50字节/秒
        0xE0, 0x07, 0xC0, 0x0F, // 预留字段
        video_stream_id,        // 视频stream_id（0xE0-0xEF）
        0x20, 0x00,             // 视频buffer bound（0x2000，约1MB）
        audio_stream_id,        // 音频stream_id（0xC0-0xDF）
        0x01, 0x00              // 音频buffer bound（0x0100，约32KB）
    };
    return {system_header, system_header + sizeof(system_header)};
}

std::vector<uint8_t> HeaderBuilder::buildPsMap() {
    std::vector<uint8_t> psm;

    // 起始码和 PS Map ID
    psm.push_back(0x00);
    psm.push_back(0x00);
    psm.push_back(0x01);
    psm.push_back(0xBC);

    // PSM 长度占位符 (稍后填充)
    const size_t length_pos = psm.size();
    psm.push_back(0x00);
    psm.push_back(0x00);

    // 当前有效标志、版本号、保留位
    psm.push_back(0xE1); // 当前有效(1) + 版本1(00001) + 保留(11)
    psm.push_back(0xFF); // 保留(1111111) + 标记位(1)

    // Program Stream Info Length
    psm.push_back(0x00);
    psm.push_back(0x00);

    // 记录 ES Map 长度字段位置
    const size_t es_map_length_pos = psm.size();
    psm.push_back(0x00);
    psm.push_back(0x00);

    // 记录 ES Map 起始位置（用于计算实际长度）
    const size_t es_map_start = psm.size();

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

    // 回填 Elementary Stream Map Length
    const uint16_t es_map_length = psm.size() - es_map_start;
    psm[es_map_length_pos] = (es_map_length >> 8) & 0xFF;
    psm[es_map_length_pos + 1] = es_map_length & 0xFF;

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

std::vector<uint8_t> HeaderBuilder::buildPsPackHeader(uint64_t pts_90k) {
    std::vector<uint8_t> ps_header(14);

    // 起始码【4字节】
    ps_header[0] = 0x00;
    ps_header[1] = 0x00;
    ps_header[2] = 0x01;
    ps_header[3] = 0xBA;

    // SCR【6字节 = 48位】——33位数据，其他位是标志位，90kHz时钟
    const auto scr = pts_90k & 0x1FFFFFFFFULL; // 强制保留低 33 位
    constexpr auto scr_ext = 0;                // SCR extension，通常为 0
    // byte[4]:  '0100' + scr[32:30] + '1' + scr[29:28]
    ps_header[4] = 0x40             // '0100 0000' 起始标记
            | ((scr >> 27) & 0x38)  // scr[32:30] 移到 bit[5: 3]
            | 0x04                  // marker_bit = 1 在 bit[2]
            | ((scr >> 28) & 0x03); // scr[29:28] 移到 bit[1:0]

    // byte[5]: scr[27:20]
    ps_header[5] = (scr >> 20) & 0xFF;

    // byte[6]: scr[19:15] + '1' + scr[14:13]
    ps_header[6] = ((scr >> 12) & 0xF8) // scr[19:15] 移到 bit[7:3]
            | 0x04                      // marker_bit = 1 在 bit[2]
            | ((scr >> 13) & 0x03);     // scr[14:13] 移到 bit[1:0]

    // byte[7]:  scr[12:5]
    ps_header[7] = (scr >> 5) & 0xFF;

    // byte[8]: scr[4:0] + '1' + scr_ext[8:7]
    ps_header[8] = ((scr << 3) & 0xF8) // scr[4:0] 移到 bit[7:3]
            | 0x04                     // marker_bit = 1 在 bit[2]
            | ((scr_ext >> 7) & 0x03); // scr_ext[8:7] 移到 bit[1:0]

    // byte[9]: scr_ext[6:0] + '1'
    ps_header[9] = ((scr_ext << 1) & 0xFE) // scr_ext[6:0] 移到 bit[7:1]
            | 0x01;                        // marker_bit = 1 在 bit[0]

    // 码流率，直接用最大值模板，提高兼容性
    ps_header[10] = 0xFF;
    ps_header[11] = 0xFF;
    ps_header[12] = 0xFC;
    ps_header[13] = 0x00;
    return ps_header;
}
