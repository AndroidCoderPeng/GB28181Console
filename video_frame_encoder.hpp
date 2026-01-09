//
// Created by peng on 2025/12/20.
//

#ifndef GB28181_VIDEO_FRAME_ENCODER_HPP
#define GB28181_VIDEO_FRAME_ENCODER_HPP

#include <opencv2/core/mat.hpp>
#include <memory>
#include <mutex>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
}

class VideoFrameEncoder {
public:
    explicit VideoFrameEncoder() = default;

    ~VideoFrameEncoder();

    bool prepare();

    /**
     * @brief 编码帧
     */
    void encodeFrame(const std::shared_ptr<cv::Mat> &mat_ptr) const;

    void stopEncode() const;

private:
    const AVCodec *_av_codec_ptr = nullptr;
    AVCodecContext *_av_codec_ctx_ptr = nullptr;

    mutable std::mutex _encoder_mutex;
    mutable std::atomic<int64_t> _frame_count{0}; // 原子计数器
};


#endif //GB28181_VIDEO_FRAME_ENCODER_HPP
