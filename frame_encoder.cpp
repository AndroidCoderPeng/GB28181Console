//
// Created by peng on 2026/1/20.
//

#include "frame_encoder.hpp"
#include "base_config.hpp"
#include "ps_muxer.hpp"

#include <iostream>
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

FrameEncoder::FrameEncoder(const size_t bufferSize) {
    _ringBuffer.capacity = bufferSize;
    _ringBuffer.frames.reserve(bufferSize);

    // 初始化FFmpeg
    const AVCodec* codecPtr = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codecPtr) {
        std::cerr << "H.264 codec not found" << std::endl;
        return;
    }

    _codecContextPtr = avcodec_alloc_context3(codecPtr);
    _codecContextPtr->width = VIDEO_WIDTH;
    _codecContextPtr->height = VIDEO_HEIGHT;
    _codecContextPtr->time_base = {1, VIDEO_FPS}; // 25fps
    _codecContextPtr->pix_fmt = AV_PIX_FMT_YUV420P;
    _codecContextPtr->bit_rate = VIDEO_BIT_RATE; // 2Mbps
    _codecContextPtr->gop_size = VIDEO_FPS;      // GOP大小
    _codecContextPtr->max_b_frames = 0;          // 实时流不用B帧

    // 设置编码参数
    av_opt_set(_codecContextPtr->priv_data, "preset", "ultrafast", 0);
    av_opt_set(_codecContextPtr->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(_codecContextPtr, codecPtr, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return;
    }

    // 分配帧和包
    _framePtr = av_frame_alloc();
    _framePtr->format = _codecContextPtr->pix_fmt;
    _framePtr->width = _codecContextPtr->width;
    _framePtr->height = _codecContextPtr->height;
    av_frame_get_buffer(_framePtr, 0);

    // 分配包
    _packetPtr = av_packet_alloc();

    // 初始化SwsContext（用于BGR到YUV420P转换）
    _swsContextPtr = sws_getContext(
                                    _codecContextPtr->width, _codecContextPtr->height, AV_PIX_FMT_BGR24,
                                    _codecContextPtr->width, _codecContextPtr->height, AV_PIX_FMT_YUV420P,
                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
}

void FrameEncoder::pushFrame(const cv::Mat& frame) {
    if (frame.empty() || !frame.data)
        return;

    std::lock_guard<std::mutex> lock(_mutex);

    // 环形缓冲区写入逻辑
    if (_ringBuffer.frames.size() < _ringBuffer.capacity) {
        // 初始化阶段：直接添加
        _ringBuffer.frames.push_back(frame.clone());
    } else {
        // 正常运行：覆盖最旧帧
        _ringBuffer.frames[_ringBuffer.writeIndex] = frame.clone();
    }

    _ringBuffer.writeIndex = (_ringBuffer.writeIndex + 1) % _ringBuffer.capacity;
    if (_ringBuffer.count < _ringBuffer.capacity) {
        ++_ringBuffer.count;
    }

    // 触发处理（如果未在处理中）
    if (!_isProcessing && _ringBuffer.count > 0) {
        _isProcessing = true;
        // lock.unlock(); TODO 解锁？
        handleFrame();
    }
}

void FrameEncoder::handleFrame() {
    cv::Mat frame;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_ringBuffer.count == 0) {
            _isProcessing = false;
            return;
        }

        frame = _ringBuffer.frames[_ringBuffer.readIndex];
        _ringBuffer.readIndex = (_ringBuffer.readIndex + 1) % _ringBuffer.capacity;
        --_ringBuffer.count;
    }

    // 编码
    {
        // 确保AVFrame可写
        if (av_frame_make_writable(_framePtr) < 0) {
            std::cerr << "Could not make frame writable" << std::endl;
            return;
        }

        // BGR到YUV420P转换
        const uint8_t* srcSlice[1] = {frame.data};
        const int srcStride[1] = {static_cast<int>(frame.step[0])};

        sws_scale(_swsContextPtr, srcSlice, srcStride, 0, frame.rows, _framePtr->data, _framePtr->linesize);

        // 发送帧给编码器
        if (avcodec_send_frame(_codecContextPtr, _framePtr) < 0) {
            std::cerr << "Error sending frame to encoder" << std::endl;
            return;
        }

        // 接收编码后的包
        while (avcodec_receive_packet(_codecContextPtr, _packetPtr) >= 0) {
            const std::vector<uint8_t> h264Buffer(_packetPtr->data, _packetPtr->data + _packetPtr->size);
            _h264DataCallback(h264Buffer);
            av_packet_unref(_packetPtr);
        }
    }

    // 继续处理下一帧
    handleFrame();
}

FrameEncoder::~FrameEncoder() {
    if (_swsContextPtr) {
        sws_freeContext(_swsContextPtr);
    }
    if (_codecContextPtr) {
        avcodec_free_context(&_codecContextPtr);
    }
    if (_framePtr) {
        av_frame_free(&_framePtr);
    }
    if (_packetPtr) {
        av_packet_free(&_packetPtr);
    }
}