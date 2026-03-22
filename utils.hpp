//
// Created by peng on 2025/12/21.
//

#ifndef GB28181CONSOLE_UTILS_HPP
#define GB28181CONSOLE_UTILS_HPP

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "logger.hpp"

class Utils {
public:
    explicit Utils();

    static Utils* get() {
        static Utils instance;
        return &instance;
    }

    Utils(const Utils&) = delete;

    Utils& operator=(const Utils&) = delete;

    uint32_t calculateCRC32(const std::vector<uint8_t>& data, size_t start, size_t length);

    std::string randomSsrc();

    std::string bytesToHex(const std::vector<uint8_t>& data, size_t length);

private:
    Logger _logger;
    std::random_device _rd;
    std::mt19937 _gen{_rd()};
    std::uniform_int_distribution<> _dis{100000, 999999};
};

#endif //GB28181CONSOLE_UTILS_HPP
