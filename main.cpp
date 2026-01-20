#include <condition_variable>
#include <iostream>
#include <thread>
#include <opencv2/opencv.hpp>

#include "base_config.hpp"
#include "sip_register.hpp"
#include "video_frame_encoder.hpp"

bool is_video_capturing = false;
std::queue<std::shared_ptr<cv::Mat>> capture_frame_queue;
std::mutex capture_queue_mutex;
std::condition_variable capture_queue_cv;
bool is_registered = false;
bool is_pushing_stream = false;
VideoFrameEncoder* video_encoder_ptr = nullptr;

void captureFrame(cv::VideoCapture& capture)
{
    while (is_video_capturing && capture.isOpened())
    {
        auto start_time = std::chrono::steady_clock::now();

        cv::Mat frame;
        capture >> frame;
        if (!frame.empty())
        {
            // 画面采集队列 - 线程安全操作
            {
                const auto shared_frame = std::make_shared<cv::Mat>(frame.clone());
                std::lock_guard<std::mutex> lock(capture_queue_mutex);
                if (capture_frame_queue.size() >= VIDEO_FPS)
                {
                    // 队列长度超过30帧，则丢弃旧帧
                    capture_frame_queue.pop();
                }
                capture_frame_queue.push(shared_frame);
            }
            capture_queue_cv.notify_one();
        }
        else
        {
            std::cerr << "Failed to read frame" << std::endl;
            break;
        }

        // 控制帧率 (约33ms间隔对应30fps)
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (elapsed.count() < 33)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed.count()));
        }
    }
}

void consumeFrame()
{
    while (true)
    {
        std::shared_ptr<cv::Mat> mat_ptr;
        {
            std::unique_lock<std::mutex> lock(capture_queue_mutex);
            capture_queue_cv.wait(lock, []
            {
                return !capture_frame_queue.empty() || !is_video_capturing;
            });

            // 如果退出标志设置且队列为空，则退出
            if (!is_video_capturing && capture_frame_queue.empty())
            {
                break;
            }

            if (!capture_frame_queue.empty())
            {
                mat_ptr = capture_frame_queue.front();
                capture_frame_queue.pop();
            }
        }

        if (mat_ptr && is_pushing_stream && is_video_capturing)
        {
            detection_worker_ptr->detectFrame(mat_ptr);
        }
    }
}

int main()
{
    // 画面采集
    cv::VideoCapture capture;
    capture.open(0, cv::CAP_V4L2);
    if (!capture.isOpened())
    {
        std::cerr << "Can not open camera" << std::endl;
        return -1;
    }

    // 设置分辨率
    capture.set(cv::CAP_PROP_FRAME_WIDTH, FRAME_WIDTH);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
    capture.set(cv::CAP_PROP_FPS, VIDEO_FPS); // 决定了摄像头硬件的最大输出频率

    // 视频捕获线程-生产者
    std::thread capture_producer_thread(captureFrame, std::ref(capture));
    capture_producer_thread.detach();
    is_video_capturing = true;

    // 视频捕获线程-消费者
    std::thread capture_consumer_thread(consumeFrame);
    capture_consumer_thread.detach();
    std::cout << "启动完成, 摄像头采集中" << std::endl;

    // Sip注册
    const auto sip_register = new SipRegister(
        "192.168.3.131",
        "111.198.10.15",
        22117,
        "11010800002000000002",
        "1101080000",
        "11010800001300011118",
        "",
        "L1300011118",
        "1234qwer",
        116.3975,
        39.9085);
    sip_register->sipEventCallback([](const int code, const std::string& message)
    {
        if (code == 200)
        {
            is_registered = true;
            // 初始化编码器
            video_encoder_ptr = new VideoFrameEncoder();
            if (!video_encoder_ptr->prepare())
            {
                std::cerr << "Failed to prepare video encoder" << std::endl;
            }
        }
        else if (code == 201)
        {
            is_registered = false;
        }
        else if (code == 1000)
        {
            is_pushing_stream = true;
        }
        else if (code == 1001)
        {
            is_pushing_stream = false;
        }
        else
        {
            std::cout << "响应码：" << code << "，内容" << message << std::endl;
        }
    });
    return 0;
}
