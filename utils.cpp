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
 * 分割 H.264 数据为 NALU，ffmpeg编码后的H.264带有起始码 0x00000001。
 * - Android端硬编码得到的H.264数据不同，NALU之间没有起始码，需要自行添加。
 *
 * @param h264
 * @param len
 * */
std::vector<std::vector<uint8_t>> Utils::splitNalUnits(const uint8_t* h264, const size_t len) {
    if (h264 == nullptr || len == 0) {
        return {};
    }

    static auto frame_count = 0;
    if (frame_count < 50) {
        std::string h264_str;
        const size_t print_len = std::min(len, static_cast<size_t>(48));
        h264_str.reserve(print_len * 3);
        for (size_t i = 0; i < print_len; i++) {
            char hex_byte[4];
            snprintf(hex_byte, sizeof(hex_byte), "%02X ", h264[i]);
            h264_str += hex_byte;
        }
        std::cout << "\n【H.264输入】帧#" << frame_count << " 前" << print_len << "字节: " << h264_str.c_str() << std::endl;
        std::cout << "【H.264输入】总长度: " << len << "字节" << std::endl;
        frame_count++;
    }

    std::vector<std::vector<uint8_t>> nalUnits;
    nalUnits.reserve(16); // 预分配，典型一帧有8-16个NALU

    size_t pos = 0;

    // ========== 阶段1：找到第一个有效起始码 ==========
    while (pos + 3 < len) {
        if (h264[pos] == 0 && h264[pos + 1] == 0) {
            if (h264[pos + 2] == 1) {
                // 找到3字节起始码 0x000001
                pos += 3;
                break;
            }
            if (pos + 3 < len && h264[pos + 2] == 0 && h264[pos + 3] == 1) {
                // 找到4字节起始码 0x00000001
                pos += 4;
                break;
            }
        }
        pos++;
    }

    if (pos >= len) {
        std::cerr << "【错误】未找到有效起始码" << std::endl;
        return {};
    }

    if (frame_count < 50) {
        std::cout << "【起始码】找到起始码，NALU数据起始位置: " << pos << std::endl;
    }

    // ========== 阶段2：提取所有NALU ==========
    while (pos < len) {
        const size_t naluStart = pos;

        // 查找下一个起始码
        while (pos + 3 < len) {
            if (h264[pos] == 0 && h264[pos + 1] == 0) {
                if (h264[pos + 2] == 1) {
                    // 找到3字节起始码
                    break;
                }
                if (pos + 3 < len && h264[pos + 2] == 0 && h264[pos + 3] == 1) {
                    // 找到4字节起始码
                    break;
                }
            }
            pos++;
        }

        // ========== 提取当前NALU ==========
        size_t naluSize = 0;
        bool hasNextNalu = false;
        int nextStartCodeSize = 0;

        if (pos + 3 < len) {
            // 找到了下一个起始码
            naluSize = pos - naluStart;
            hasNextNalu = true;

            // 判断是3字节还是4字节起始码
            if (h264[pos + 2] == 1) {
                nextStartCodeSize = 3; // 0x000001
            } else {
                nextStartCodeSize = 4; // 0x00000001
            }
        } else {
            // 已经是最后一个NALU（没有下一个起始码）
            naluSize = len - naluStart;
            hasNextNalu = false;
        }

        // 保存NALU（不包含起始码）
        if (naluSize > 0) {
            nalUnits.emplace_back(h264 + naluStart, h264 + naluStart + naluSize);

            if (frame_count < 50) {
                const uint8_t nalType = h264[naluStart] & 0x1F;
                auto nalTypeStr = "";
                switch (nalType) {
                    case 5:
                        nalTypeStr = "IDR帧";
                        break;
                    case 7:
                        nalTypeStr = "SPS";
                        break;
                    case 8:
                        nalTypeStr = "PPS";
                        break;
                    case 1:
                        nalTypeStr = "非IDR片";
                        break;
                    case 6:
                        nalTypeStr = "SEI";
                        break;
                    default:
                        nalTypeStr = "其他";
                        break;
                }
                std::cout << "【提取NALU】#" << nalUnits.size()
                        << " 类型:" << nalTypeStr << "(" << static_cast<int>(nalType) << ")"
                        << " 大小:" << naluSize << "字节"
                        << " 起始码:" << (hasNextNalu ? (nextStartCodeSize == 3 ? "3字节" : "4字节") : "无")
                        << std::endl;
            }
        } else {
            std::cerr << "【警告】NALU大小为0，位置:" << naluStart << std::endl;
        }

        // 跳到下一个NALU起始位置
        if (hasNextNalu) {
            pos += nextStartCodeSize;
        } else {
            break; // 已经是最后一个NALU
        }
    }

    if (frame_count < 50) {
        std::cout << "【完成】共提取 " << nalUnits.size() << " 个NALU" << std::endl;
        frame_count++;
    }

    return nalUnits;
}

/**
 * 验证NALU数组是否有效
 * - P帧NALU至少应该大于20字节
 * - IDR帧NALU至少应该大于100字节
 * - 过滤明显损坏的数据
 */
bool Utils::validateNalUnits(const std::vector<std::vector<uint8_t>>& nalUnits) {
    if (nalUnits.empty()) {
        std::cerr << "[VALIDATE] NALU数组为空" << std::endl;
        return false;
    }

    // 统计有效的视频NALU（非SPS/PPS/SEI）
    size_t validVideoNaluCount = 0;
    size_t totalVideoNaluSize = 0;

    for (size_t i = 0; i < nalUnits.size(); i++) {
        const auto& nalu = nalUnits[i];
        if (nalu.empty()) {
            std::cerr << "[VALIDATE] NALU[" << i << "] 为空" << std::endl;
            return false;
        }

        const uint8_t nalType = nalu[0] & 0x1F;

        // 检查视频NALU的大小
        if (nalType == 1 || nalType == 5) {
            // 非IDR片或IDR帧
            if (nalu.size() < 20) {
                std::cerr << "[VALIDATE] NALU[" << i << "] 类型=" << static_cast<int>(nalType)
                        << " 大小=" << nalu.size() << " 过小，可能是损坏数据" << std::endl;
                return false;
            }
            validVideoNaluCount++;
            totalVideoNaluSize += nalu.size();
        }
    }

    // 必须至少有一个有效的视频NALU
    if (validVideoNaluCount == 0) {
        std::cerr << "[VALIDATE] 未找到有效的视频NALU" << std::endl;
        return false;
    }

    // 平均视频NALU大小不应该太小
    const size_t avgSize = totalVideoNaluSize / validVideoNaluCount;
    if (avgSize < 50) {
        std::cerr << "[VALIDATE] 平均视频NALU大小=" << avgSize << " 过小，可能是损坏数据" << std::endl;
        return false;
    }

    return true;
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