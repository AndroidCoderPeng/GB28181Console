//
// Created by peng on 2025/12/21.
//

#ifndef GB28181_HEADER_BUILDER_HPP
#define GB28181_HEADER_BUILDER_HPP

#include <cstdint>
#include <vector>

class HeaderBuilder {
public:
    /**
     * 给 SPS/PPS/IDR/P 帧添加起始码
     * @param dst
     * @param data
     * @param len
     */
    static void insertStartCode(std::vector<uint8_t>& dst, const uint8_t* data, size_t len);

    static std::vector<uint8_t> buildPesHeader(uint8_t stream_id, size_t len, uint64_t pts_90k);

    /**
     * 只要不加路数、不特殊缓冲策略，系统头就是固定值
     * */
    static std::vector<uint8_t> buildSystemHeader(uint8_t video_stream_id, uint8_t audio_stream_id);

    /**
     * PSM除了CRC-32之外的其他各个字节在内容（流类型、ID、长度、头部）确定的情况下，都是固定不变的
     */
    static std::vector<uint8_t> buildPsMap();

    static std::vector<uint8_t> buildPsPackHeader(uint64_t pts_90k);
};

#endif //GB28181_HEADER_BUILDER_HPP
