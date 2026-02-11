//
// Created by pengx on 2026/2/11.
//

#include "h264_splitter.hpp"

#include <iostream>

int H264Splitter::splitH264Frame(const uint8_t* frame, const size_t frame_size, std::vector<NALU>& nalu_vector) {
    if (frame == nullptr || frame_size == 0) {
        return 0;
    }

    nalu_vector.clear();
    size_t i = 0;

    // 查找所有起始码位置
    std::vector<size_t> start_positions;
    std::vector<size_t> start_code_lengths;

    while (i < frame_size) {
        // 检查 4 字节起始码
        if (i + 3 < frame_size &&
            frame[i] == 0x00 &&
            frame[i + 1] == 0x00 &&
            frame[i + 2] == 0x00 &&
            frame[i + 3] == 0x01) {
            start_positions.push_back(i);
            start_code_lengths.push_back(4);
            i += 4;
            continue;
        }
        // 检查 3 字节起始码
        if (i + 2 < frame_size &&
            frame[i] == 0x00 &&
            frame[i + 1] == 0x00 &&
            frame[i + 2] == 0x01) {
            start_positions.push_back(i);
            start_code_lengths.push_back(3);
            i += 3;
            continue;
        }
        i++;
    }

    // 如果没找到起始码
    if (start_positions.empty()) {
        std::cerr << "未找到任何起始码" << std::endl;
        return 0;
    }

    // 根据起始码位置提取 NALU
    for (size_t idx = 0; idx < start_positions.size(); idx++) {
        const size_t nalu_start = start_positions[idx] + start_code_lengths[idx];
        size_t nalu_end;

        if (idx + 1 < start_positions.size()) {
            nalu_end = start_positions[idx + 1];
        } else {
            nalu_end = frame_size;
        }

        const size_t nalu_size = nalu_end - nalu_start;

        if (nalu_size > 0 && nalu_start < frame_size) {
            NALU nalu;
            nalu.data = const_cast<uint8_t*>(frame + nalu_start);
            nalu.size = nalu_size;
            nalu.type = nalu.data[0] & 0x1F;

            nalu_vector.push_back(nalu);
        }
    }
    return nalu_vector.size();
}
