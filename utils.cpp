//
// Created by peng on 2025/12/21.
//

#include "utils.hpp"

Utils::Utils() : _logger("Utils") {
    _logger.i("Utils created");
}

uint32_t Utils::calculateCRC32(const std::vector<uint8_t>& data, const size_t start, const size_t length) { // NOLINT
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
    return "0108" + std::to_string(_dis(_gen));
}

std::string Utils::bytesToHex(const std::vector<uint8_t> &data, size_t length) { // NOLINT
    if (data.empty() || length == 0) {
        return "";
    }
    std::string str;
    const size_t len = std::min(length, data.size());
    str.reserve(len * 3);
    for (size_t i = 0; i < len; i++) {
        char hex_byte[4];
        snprintf(hex_byte, sizeof(hex_byte), "%02X", data[i]);
        str += hex_byte;
        if (i < len - 1) str += " ";  // 只在中间加空格，末尾不加
    }
    return str;
}