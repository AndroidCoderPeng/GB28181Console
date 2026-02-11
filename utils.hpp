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

    static std::string randomSsrc();
};

#endif //GB28181_UTILS_HPP
