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

    _codec_ctx_ptr = avcodec_alloc_context3(codecPtr);
    _codec_ctx_ptr->width = VIDEO_WIDTH;
    _codec_ctx_ptr->height = VIDEO_HEIGHT;
    _codec_ctx_ptr->time_base = {1, VIDEO_FPS}; // 25fps
    _codec_ctx_ptr->pix_fmt = AV_PIX_FMT_YUV420P;
    _codec_ctx_ptr->bit_rate = VIDEO_BIT_RATE; // 2Mbps
    _codec_ctx_ptr->gop_size = VIDEO_FPS;      // GOP大小
    _codec_ctx_ptr->max_b_frames = 0;          // 实时流不用B帧

    // 设置编码参数
    av_opt_set(_codec_ctx_ptr->priv_data, "preset", "ultrafast", 0);
    av_opt_set(_codec_ctx_ptr->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(_codec_ctx_ptr, codecPtr, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return;
    }

    // 分配帧和包
    _frame_ptr = av_frame_alloc();
    _frame_ptr->format = _codec_ctx_ptr->pix_fmt;
    _frame_ptr->width = _codec_ctx_ptr->width;
    _frame_ptr->height = _codec_ctx_ptr->height;
    av_frame_get_buffer(_frame_ptr, 0);

    // 分配包
    _packet_ptr = av_packet_alloc();

    // 初始化SwsContext（用于BGR到YUV420P转换）
    _sws_ctx_ptr = sws_getContext(
                                    _codec_ctx_ptr->width, _codec_ctx_ptr->height, AV_PIX_FMT_BGR24,
                                    _codec_ctx_ptr->width, _codec_ctx_ptr->height, AV_PIX_FMT_YUV420P,
                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
}

void FrameEncoder::pushFrame(const cv::Mat& frame) {
    if (frame.empty() || !frame.data)
        return;

    std::unique_lock<std::mutex> lock(_mutex);

    // 环形缓冲区写入逻辑
    if (_ringBuffer.frames.size() < _ringBuffer.capacity) {
        _ringBuffer.frames.push_back(frame.clone());
    } else {
        _ringBuffer.frames[_ringBuffer.writeIndex] = frame.clone();
    }

    _ringBuffer.writeIndex = (_ringBuffer.writeIndex + 1) % _ringBuffer.capacity;
    if (_ringBuffer.count < _ringBuffer.capacity) {
        ++_ringBuffer.count;
    }

    // 触发处理（如果未在处理中）
    if (!_isProcessing && _ringBuffer.count > 0) {
        _isProcessing = true;
        lock.unlock();
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
        if (av_frame_make_writable(_frame_ptr) < 0) {
            std::cerr << "Could not make frame writable" << std::endl;
            return;
        }

        // BGR到YUV420P转换
        const uint8_t* srcSlice[1] = {frame.data};
        const int srcStride[1] = {static_cast<int>(frame.step[0])};

        sws_scale(_sws_ctx_ptr, srcSlice, srcStride, 0, frame.rows, _frame_ptr->data, _frame_ptr->linesize);

        // 发送帧给编码器
        if (avcodec_send_frame(_codec_ctx_ptr, _frame_ptr) < 0) {
            std::cerr << "Error sending frame to encoder" << std::endl;
            return;
        }

        // 接收编码后的包
        while (avcodec_receive_packet(_codec_ctx_ptr, _packet_ptr) >= 0) {
            const std::vector<uint8_t> h264Buffer(_packet_ptr->data, _packet_ptr->data + _packet_ptr->size);
            _h264_callback(h264Buffer);
            av_packet_unref(_packet_ptr);
        }
    }

    // 继续处理下一帧
    handleFrame();
}

FrameEncoder::~FrameEncoder() {
    if (_sws_ctx_ptr) {
        sws_freeContext(_sws_ctx_ptr);
    }
    if (_codec_ctx_ptr) {
        avcodec_free_context(&_codec_ctx_ptr);
    }
    if (_frame_ptr) {
        av_frame_free(&_frame_ptr);
    }
    if (_packet_ptr) {
        av_packet_free(&_packet_ptr);
    }
}