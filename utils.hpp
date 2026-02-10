//
// Created by peng on 2025/12/21.
//

#ifndef GB28181_UTILS_HPP
#define GB28181_UTILS_HPP

#include <cstdint>
#include <vector>
#include <string>

class Utils {
public:
    static uint32_t calculateCRC32(const std::vector<uint8_t>& data, size_t start, size_t length);

    static std::vector<std::vector<uint8_t>> splitNalUnits(const uint8_t* h264, size_t len);

    /**
     * 验证NALU数组是否有效
     * - 过滤过小的NALU（可能是损坏的数据）
     * - 确保至少包含有效的视频数据
     *
     * @param nalUnits NALU数组
     * @return true如果有效，false如果应该丢弃
     */
    static bool validateNalUnits(const std::vector<std::vector<uint8_t>>& nalUnits);

    static std::string randomSsrc();
};

#endif //GB28181_UTILS_HPP
