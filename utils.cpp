//
// Created by peng on 2025/12/21.
//

#include "utils.hpp"

uint32_t Utils::calculateCRC32(const std::vector<uint8_t>& data, size_t start, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = start; i < start + length; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * 分割 H.264 数据为 NALU，ffmpeg编码后的H.264带有起始码 0x00000001。
 * - Android端硬编码得到的H.264数据不同，NALU之间没有起始码，需要自行添加。
 *
 * @param h264
 * @param len
 * */
std::vector<std::vector<uint8_t>> Utils::splitNalUnits(const uint8_t* h264, const size_t len)
{
    std::vector<std::vector<uint8_t>> nalUnits{};
    if (len < 4) return nalUnits;

    size_t i = 0;

    // 跳过开头的起始码
    while (i + 2 < len)
    {
        if ((h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 1) ||
            (i + 3 < len && h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 0 && h264[i + 3] == 1))
        {
            i += (h264[i + 2] == 1) ? 3 : 4;
            break;
        }
        i++;
    }

    // 逐个提取NALU（不包含起始码）
    while (i < len)
    {
        const size_t naluStart = i;

        // 查找下一个起始码
        while (i + 2 < len)
        {
            if ((h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 1) ||
                (i + 3 < len && h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 0 && h264[i + 3] == 1))
            {
                break;
            }
            i++;
        }

        // 保存NALU（不包含起始码）
        if (i > naluStart)
        {
            nalUnits.emplace_back(h264 + naluStart, h264 + i);
        }

        // 跳过起始码
        if (i + 2 < len && h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 1)
        {
            i += 3;
        }
        else if (i + 3 < len && h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 0 && h264[i + 3] == 1)
        {
            i += 4;
        }
        else
        {
            break;
        }
    }

    return nalUnits;
}
