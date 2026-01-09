//
// Created by Administrator on 2026/1/7.
//

#ifndef FRAMEDETECTOR_DETECTION_WORKER_HPP
#define FRAMEDETECTOR_DETECTION_WORKER_HPP

#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <opencv2/core/mat.hpp>

class DetectionWorker {
public:
    using DetectCallback = std::function<void(const std::shared_ptr<cv::Mat> &)>;

    explicit DetectionWorker(const DetectCallback &callback);

    void detectFrame(const std::shared_ptr<cv::Mat> &mat_ptr);

    ~DetectionWorker();

private:
    DetectCallback _detect_callback;

    bool _detect_thread_running = false;
    // 后台线程（子线程）
    std::unique_ptr<std::thread> _detect_thread_ptr = nullptr;

    // 检测队列
    std::queue<std::shared_ptr<cv::Mat> > _detect_frame_queue;
    mutable std::mutex _detect_queue_mutex;
    std::condition_variable _detect_queue_cv;

    std::shared_ptr<cv::Mat> detect(const std::shared_ptr<cv::Mat> &mat_ptr);
};


#endif //FRAMEDETECTOR_DETECTION_WORKER_HPP
