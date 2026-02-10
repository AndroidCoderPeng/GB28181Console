//
// Created by pengx on 2026/2/10.
//

#ifndef GB28181CONSOLE_FRAME_CAPTURE_HPP
#define GB28181CONSOLE_FRAME_CAPTURE_HPP

#include <opencv2/opencv.hpp>
#include <atomic>
#include <thread>
#include <functional>

class FrameCapture {
public:
    using CameraErrorCallback = std::function<void(const std::string&)>;
    using CameraFrameCallback = std::function<void(cv::Mat mat)>;

    explicit FrameCapture(int index, const CameraErrorCallback& error_callback,
                          const CameraFrameCallback& frame_callback);

    ~FrameCapture();

    void start();

    void stop();

private:
    int _index{0};
    cv::VideoCapture _cap;
    std::thread* _thread_ptr{nullptr};
    std::atomic<bool> _is_running{false};

    // 回调函数
    CameraErrorCallback _error_callback;
    CameraFrameCallback _frame_callback;

    void capture_loop();
};


#endif //GB28181CONSOLE_FRAME_CAPTURE_HPP
