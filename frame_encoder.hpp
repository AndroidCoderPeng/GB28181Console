//
// Created by peng on 2026/1/20.
//

#ifndef GB28181CONSOLE_FRAME_ENCODER_HPP
#define GB28181CONSOLE_FRAME_ENCODER_HPP

#include <mutex>
#include <functional>
#include <opencv2/core/mat.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class FrameEncoder {
public:
    using H264DataCallback = std::function<void(std::vector<uint8_t>)>;

    explicit FrameEncoder(size_t bufferSize = 3);

    // 生产者：快速写入
    void pushFrame(const cv::Mat& frame);

    // 消费者：快速读取
    void handleFrame();

    ~FrameEncoder();

private:
    struct FrameBuffer {
        std::vector<cv::Mat> frames; // 预分配的帧缓冲区
        size_t writeIndex = 0;       // 写入位置
        size_t readIndex = 0;        // 读取位置
        size_t count = 0;            // 当前帧数
        size_t capacity;             // 容量
    } _ringBuffer;

    std::mutex _mutex;
    bool _isProcessing = false;

    AVCodecContext* _codecContextPtr = nullptr;
    AVFrame* _framePtr = nullptr;
    AVPacket* _packetPtr = nullptr;
    SwsContext* _swsContextPtr = nullptr;

    H264DataCallback _h264DataCallback;

    bool _is_push_stream = false;
};


#endif //GB28181CONSOLE_FRAME_ENCODER_HPP
