//
// Created by pengx on 2026/2/10.
//

#include "frame_capture.hpp"
#include "base_config.hpp"

#include <chrono>

FrameCapture::FrameCapture(const int index, const CameraErrorCallback& error_callback,
                           const CameraFrameCallback& frame_callback) {
    _index = index;
    _error_callback = error_callback;
    _frame_callback = frame_callback;
}

void FrameCapture::start() {
    if (!_cap.open(0, cv::CAP_V4L2)) {
        _error_callback("Cannot open camera");
        return;
    }
    // 设置参数
    _cap.set(cv::CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH);
    _cap.set(cv::CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT);
    _cap.set(cv::CAP_PROP_FPS, VIDEO_FPS);

    _is_running = true;
    _thread_ptr = new std::thread(&FrameCapture::capture_loop, this);
}

void FrameCapture::capture_loop() {
    // 约 40ms 间隔（25 FPS）
    const auto frame_interval = std::chrono::milliseconds(40);

    while (_is_running.load()) {
        auto start_time = std::chrono::steady_clock::now();
        {
            cv::Mat frame;
            _cap >> frame;
            if (!frame.empty()) {
                _frame_callback(frame);
            }
        }
        // 计算实际执行时间，确保稳定帧率
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto sleep_time = frame_interval - elapsed;
        if (sleep_time > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

void FrameCapture::stop() {
    _is_running = false;
    if (_thread_ptr && _thread_ptr->joinable()) {
        _thread_ptr->join();
        delete _thread_ptr;
        _thread_ptr = nullptr;
    }

    if (_cap.isOpened()) {
        _cap.release();
    }
}

FrameCapture::~FrameCapture() {
    stop();
}
