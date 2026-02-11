//
// Created by pengx on 2026/2/11.
//

#ifndef GB28181CONSOLE_H264_SPLITTER_HPP
#define GB28181CONSOLE_H264_SPLITTER_HPP

#include <vector>
#include <cstdint>

/**
 * @brief NALU 结构体，用于存储单个 NALU 数据
 * 1: 非 IDR 图像的编码条带
 * 5: IDR 图像的编码条带
 * 7: SPS（序列参数集）
 * 8: PPS（图像参数集）
 * 6: SEI（补充增强信息）
 */
struct NALU {
    uint8_t* data; // NALU 净荷指针（不包含起始码）
    size_t size;   // NALU 净荷大小
    int type;      // NALU 类型
};

class H264Splitter {
public:
    /**
     * @brief 分割 H.264 帧为多个 NALU
     * @param frame H.264 帧数据指针
     * @param frame_size 帧数据大小
     * @param nalu_vector 输出参数，存储分割后的 NALU 列表
     * @return 分割出的 NALU 数量
     */
    static int splitH264Frame(const uint8_t* frame, size_t frame_size, std::vector<NALU>& nalu_vector);
};


#endif //GB28181CONSOLE_H264_SPLITTER_HPP
