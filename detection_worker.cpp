//
// Created by Administrator on 2026/1/7.
//

#include "detection_worker.hpp"
#include "base_config.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace std::chrono;

static void save_frame(const cv::Mat &mat) {
    const auto now = system_clock::now();
    const auto time_t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream ss;
    std::tm tm_time;
    localtime_r(&time_t, &tm_time);
    ss << std::put_time(&tm_time, "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    const std::string filename = "IMG_" + ss.str() + ".png";
    cv::imwrite(filename, mat);
    std::cout << "检测结果保存成功" << std::endl;
}

DetectionWorker::DetectionWorker(const DetectCallback &callback) {
    _detect_callback = std::move(callback);
    _detect_thread_running = true;
    _detect_thread_ptr = std::make_unique<std::thread>([this] {
        while (true) {
            std::shared_ptr<cv::Mat> mat_ptr; {
                std::unique_lock<std::mutex> lock(_detect_queue_mutex);
                _detect_queue_cv.wait(lock, [this] {
                    return !_detect_frame_queue.empty() || !_detect_thread_running;
                });

                // 如果退出标志设置且队列为空，则退出
                if (!_detect_thread_running && _detect_frame_queue.empty()) {
                    break;
                }

                if (!_detect_frame_queue.empty()) {
                    mat_ptr = _detect_frame_queue.front();
                    _detect_frame_queue.pop();
                }
            }

            if (mat_ptr) {
                const auto detected_frame_ptr = detect(mat_ptr);
                _detect_callback(detected_frame_ptr);
            }
        }
    });
}

void DetectionWorker::detectFrame(const std::shared_ptr<cv::Mat> &mat_ptr) {
    // 画面检测队列 - 线程安全操作
    {
        std::lock_guard<std::mutex> lock(_detect_queue_mutex);
        if (_detect_frame_queue.size() >= VIDEO_FPS * 2) {
            // 队列长度超过60帧，则丢弃旧帧
            _detect_frame_queue.pop();
        }
        _detect_frame_queue.push(mat_ptr);
    }
    _detect_queue_cv.notify_one();
}

std::shared_ptr<cv::Mat> DetectionWorker::detect(const std::shared_ptr<cv::Mat> &mat_ptr) {
    const cv::Mat mat = *mat_ptr;
    cv::Mat result = mat.clone();
    // std::cout << "开始检测" << std::endl;
    // 模拟AI检测延迟150ms
    std::this_thread::sleep_for(milliseconds(150));

    // 计算图像中心位置
    const int center_x = result.cols / 2;
    const int center_y = result.rows / 2;

    // 定义红框尺寸（框的宽度和高度为图像的1/4）
    const int box_width = result.cols / 4;
    const int box_height = result.rows / 4;

    // 计算红框的左上角和右下角坐标
    const cv::Point top_left(center_x - box_width / 2, center_y - box_height / 2);
    const cv::Point bottom_right(center_x + box_width / 2, center_y + box_height / 2);

    // 绘制红色矩形框
    cv::rectangle(result, top_left, bottom_right, cv::Scalar(0, 0, 255), 2);

    // 在红框左上角添加"Casic"文字
    cv::putText(result,
                "Casic", top_left + cv::Point(0, -10),
                cv::FONT_HERSHEY_SIMPLEX,
                1,
                cv::Scalar(255, 0, 0),
                2);

    // 保存图片
    // save_frame(result);
    return std::make_shared<cv::Mat>(result);
}

DetectionWorker::~DetectionWorker() {
    _detect_thread_running = false;
    _detect_queue_cv.notify_all();
    if (_detect_thread_ptr && _detect_thread_ptr->joinable()) {
        _detect_thread_ptr->join();
        _detect_thread_ptr = nullptr;
    }
}
