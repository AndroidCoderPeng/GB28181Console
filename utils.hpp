//
// Created by peng on 2025/12/21.
//

#ifndef GB28181_UTILS_HPP
#define GB28181_UTILS_HPP

#include <cstdint>
#include <vector>

class Utils
{
public:
    static uint32_t calculateCRC32(const std::vector<uint8_t>& data, size_t start, size_t length);

    static std::vector<std::vector<uint8_t>> splitNalUnits(const uint8_t* h264, size_t len);
};

#endif //GB28181_UTILS_HPP
