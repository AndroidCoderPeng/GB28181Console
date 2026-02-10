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
    _ringBuffer.frames.resize(bufferSize);

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

void FrameEncoder::setH264DataCallback(const H264DataCallback& callback) {
    _h264_callback = callback;
}

void FrameEncoder::pushFrame(const cv::Mat& frame) {
    if (frame.empty() || !frame.data)
        return;

    std::unique_lock<std::mutex> lock(_mutex);

    if (_ringBuffer.frames.empty()) {
        _ringBuffer.frames.resize(_ringBuffer.capacity);
    }

    // 写入帧（覆盖旧数据）
    _ringBuffer.frames[_ringBuffer.writeIndex] = frame.clone();
    _ringBuffer.writeIndex = (_ringBuffer.writeIndex + 1) % _ringBuffer.capacity;

    if (_ringBuffer.count < _ringBuffer.capacity) {
        _ringBuffer.count++;
    } else {
        // 缓冲区满，移动读指针（丢弃最旧帧）
        _ringBuffer.readIndex = (_ringBuffer.readIndex + 1) % _ringBuffer.capacity;
    }

    _encode_cv.notify_one(); // 通知编码线程
}

void FrameEncoder::start() {
    _is_running = true;
    _encode_thread_ptr = new std::thread(&FrameEncoder::encode_loop, this);
}

void FrameEncoder::encode_loop() {
    while (_is_running) {
        std::unique_lock<std::mutex> lock(_mutex);

        // 等待有数据或停止信号
        _encode_cv.wait(lock, [this] {
            return !_is_running || _ringBuffer.count > 0;
        });

        if (!_is_running)
            break;

        // 处理所有待编码帧
        while (_ringBuffer.count > 0 && _is_running) {
            // 取出帧（不阻塞）
            cv::Mat frame = _ringBuffer.frames[_ringBuffer.readIndex];
            _ringBuffer.readIndex = (_ringBuffer.readIndex + 1) % _ringBuffer.capacity;
            _ringBuffer.count--;

            lock.unlock(); // 释放锁，允许pushFrame

            // 执行编码（耗时操作，不持有锁）
            encode_frame(frame);

            lock.lock(); // 重新加锁
        }
    }
}

void FrameEncoder::encode_frame(const cv::Mat& frame) const {
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

void FrameEncoder::stop() {
    _is_running = false;
    // 唤醒等待线程
    _encode_cv.notify_all();
    if (_encode_thread_ptr && _encode_thread_ptr->joinable()) {
        _encode_thread_ptr->join();
        delete _encode_thread_ptr;
        _encode_thread_ptr = nullptr;
    }
}

FrameEncoder::~FrameEncoder() {
    stop();
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
