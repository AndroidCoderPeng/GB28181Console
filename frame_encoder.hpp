//
// Created by peng on 2026/1/20.
//

#ifndef GB28181CONSOLE_FRAME_ENCODER_HPP
#define GB28181CONSOLE_FRAME_ENCODER_HPP

#include <atomic>
#include <mutex>
#include <opencv2/core/mat.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
}

class FrameEncoder
{
public:
    ~FrameEncoder();

    bool prepare();

    void encodeFrame(const cv::Mat& frame);

    void startStream();

    void stopStream();

    void stopEncode();

private:
    const AVCodec* _av_codec_ptr = nullptr;
    AVCodecContext* _av_codec_ctx_ptr = nullptr;

    std::mutex _encoder_mutex;
    std::atomic<int64_t> _frame_count{0}; // 原子计数器

    bool _is_push_stream = false;
};


#endif //GB28181CONSOLE_FRAME_ENCODER_HPP
