//
// Created by peng on 2026/1/4.
//

#ifndef GB28181_PS_MUXER_HPP
#define GB28181_PS_MUXER_HPP

#include <mutex>
#include <vector>

/**
 * PS 的基本单位包括：
 *  - Pack Header（包头）：表明数据块的起始点，包含时间基准（SCR）和多路复用速率。
 *  - System Header（系统头）：描述流的同步要求，如缓冲区大小、多流特性（可选部分）。
 *  - PES（Packetized Elementary Stream）：最小的视频或音频负载单位。
 * */
class PsMuxer {
public:
    explicit PsMuxer() = default;

    static PsMuxer* get() {
        static PsMuxer instance;
        return &instance;
    }

    PsMuxer(const PsMuxer&) = delete;

    PsMuxer& operator=(const PsMuxer&) = delete;

    void writeVideoFrame(const uint8_t* h264_data, uint64_t pts_90k, size_t size);

    void writeAudioFrame(const uint8_t* pcm_data, uint64_t pts_90k, size_t size);

    void release();

private:
    std::vector<uint8_t> _sps_cache{};
    std::vector<uint8_t> _pps_cache{};
    bool _is_waiting_for_idr = true; // 等待接收IDR帧
    bool _is_idr_sent = false;
    std::mutex _muxer_mutex{};
};

#endif //GB28181_PS_MUXER_HPP
