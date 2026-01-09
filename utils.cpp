//
// Created by peng on 2025/12/21.
//

#include "utils.hpp"

uint32_t Utils::mpeg2Crc32(const uint8_t *data, const size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc << 8 ^ crc_table[(crc >> 24 ^ data[i]) & 0xFF];
    }
    return crc;
}

/**
 * 分割 H.264 数据为 NALU，不带起始码
 *
 * @param h264
 * @param len
 * */
std::vector<std::vector<uint8_t> > Utils::splitNalUnits(const uint8_t *h264, const size_t len) {
    std::vector<std::vector<uint8_t> > nalUnits{};
    size_t i = 0;
    while (i < len) {
        // 查找起始码 0x000001 或 0x00000001
        if (i + 2 < len && h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 1) {
            // 跳过起始码（3 字节）
            size_t naluStart = i + 3;
            // 如果是 4 字节起始码（00 00 00 01），再跳一个 0x00
            if (naluStart < len && h264[naluStart] == 0) {
                naluStart++;
            }

            // 寻找下一个起始码的位置
            size_t nextStart = naluStart;
            while (nextStart < len - 2) {
                if (h264[nextStart] == 0 && h264[nextStart + 1] == 0 && h264[nextStart + 2] == 1) {
                    break;
                }
                nextStart++;
            }

            // 如果没找到，nextStart 就是 size（取到末尾）
            if (nextStart >= len - 2) {
                nextStart = len;
            }

            // 提取 NALU payload（不含起始码！）
            nalUnits.emplace_back(h264 + naluStart, h264 + nextStart);

            // 继续从下一个起始码处开始
            i = nextStart;
        } else {
            i++;
        }
    }
    return nalUnits;
}

std::vector<std::vector<uint8_t> > Utils::splitNalUnitsFromPacket(const uint8_t *h264, const size_t len) {
    std::vector<std::vector<uint8_t> > nalUnits{};
    size_t offset = 0;

    while (offset + 4 <= len) {
        // 读取4字节长度字段（大端序）
        const uint32_t nalu_length = h264[offset] << 24
                                     | h264[offset + 1] << 16
                                     | h264[offset + 2] << 8
                                     | h264[offset + 3];

        // 检查长度有效性
        if (offset + 4 + nalu_length > len) {
            break; // 数据不完整
        }

        // 提取NALU数据（不含长度字段）
        std::vector<uint8_t> nalu(h264 + offset + 4, h264 + offset + 4 + nalu_length);
        nalUnits.push_back(nalu);

        // 移动到下一个NALU
        offset += 4 + nalu_length;
    }

    return nalUnits;
}
