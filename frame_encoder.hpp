//
// Created by peng on 2026/1/20.
//

#ifndef GB28181CONSOLE_FRAME_ENCODER_HPP
#define GB28181CONSOLE_FRAME_ENCODER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <opencv2/core/mat.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class FrameEncoder {
public:
    using H264DataCallback = std::function<void(const std::vector<uint8_t>&)>;

    explicit FrameEncoder(size_t bufferSize = 3);

    void setH264DataCallback(const H264DataCallback& callback);

    // 生产者：快速写入
    void pushFrame(const cv::Mat& frame);

    void start();

    void stop();

    ~FrameEncoder();

private:
    struct FrameBuffer {
        std::vector<cv::Mat> frames; // 预分配的帧缓冲区
        size_t writeIndex = 0;       // 写入位置
        size_t readIndex = 0;        // 读取位置
        size_t count = 0;            // 当前帧数
        size_t capacity;             // 容量
    } _ringBuffer;

    AVCodecContext* _codec_ctx_ptr = nullptr;
    AVFrame* _frame_ptr = nullptr;
    AVPacket* _packet_ptr = nullptr;
    SwsContext* _sws_ctx_ptr = nullptr;

    // 编码相关
    std::thread* _encode_thread_ptr = nullptr;
    std::mutex _mutex;
    std::atomic<bool> _is_running{false};
    std::condition_variable _encode_cv;

    void encode_loop();

    void encode_frame(const cv::Mat& frame) const;

    H264DataCallback _h264_callback;
};


#endif //GB28181CONSOLE_FRAME_ENCODER_HPP
