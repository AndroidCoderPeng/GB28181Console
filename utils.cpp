//
// Created by peng on 2025/12/21.
//

#include "utils.hpp"

#include <iomanip>
#include <iostream>
#include <random>

uint32_t Utils::calculateCRC32(const std::vector<uint8_t>& data, size_t start, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = start; i < start + length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * SSRC = 设备类型代码(2位) + 厂商代码(2位) + 设备序列号(6位)
 * */
std::string Utils::randomSsrc() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    const int random_suffix = dis(gen);
    return "0108" + std::to_string(random_suffix);
}