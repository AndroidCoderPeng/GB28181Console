#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <queue>
#include <thread>
#include <opencv2/opencv.hpp>

#include "base_config.hpp"
#include "frame_encoder.hpp"
#include "sip_register.hpp"

class VideoCaptureManager
{
public:
    VideoCaptureManager() = default;

    ~VideoCaptureManager()
    {
        stopCapture();
    }

    bool initializeCamera()
    {
        capture = std::make_unique<cv::VideoCapture>();

        if (!capture->open(0, cv::CAP_V4L2))
        {
            std::cerr << "Cannot open camera" << std::endl;
            return false;
        }

        // 验证并设置摄像头参数
        if (!capture->set(cv::CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH))
        {
            std::cerr << "Failed to set width: " << VIDEO_WIDTH << std::endl;
        }
        if (!capture->set(cv::CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT))
        {
            std::cerr << "Failed to set height: " << VIDEO_HEIGHT << std::endl;
        }
        if (!capture->set(cv::CAP_PROP_FPS, VIDEO_FPS))
        {
            std::cerr << "Failed to set FPS: " << VIDEO_FPS << std::endl;
        }

        // 获取实际设置的参数
        const double actual_width = capture->get(cv::CAP_PROP_FRAME_WIDTH);
        const double actual_height = capture->get(cv::CAP_PROP_FRAME_HEIGHT);
        const double actual_fps = capture->get(cv::CAP_PROP_FPS);

        std::cout << "Camera configured: "
            << actual_width << "x" << actual_height
            << "@" << actual_fps << "fps" << std::endl;

        return capture->isOpened();
    }

    void captureFrame()
    {
        while (is_video_capturing.load() && capture && capture->isOpened())
        {
            auto start_time = std::chrono::steady_clock::now();

            cv::Mat frame;
            *capture >> frame;

            if (!frame.empty())
            {
                // 将帧放入队列 - 线程安全操作
                {
                    const auto shared_frame = std::make_shared<cv::Mat>(frame.clone());
                    std::lock_guard<std::mutex> lock(capture_queue_mutex);

                    // 限制队列大小，防止内存溢出
                    constexpr size_t max_queue_size = VIDEO_FPS; // 1秒的帧数
                    if (capture_frame_queue.size() >= max_queue_size)
                    {
                        // 清理一半的旧帧，而不是逐个清理
                        constexpr size_t frames_to_remove = max_queue_size / 2;
                        for (size_t i = 0; i < frames_to_remove && !capture_frame_queue.empty(); ++i)
                        {
                            capture_frame_queue.pop();
                        }
                        std::cout << "Queue overflow! Removed " << frames_to_remove << " frames." << std::endl;
                    }

                    capture_frame_queue.push(shared_frame);
                }
                capture_queue_cv.notify_one();
            }
            else
            {
                std::cerr << "Failed to read frame, attempting reconnection..." << std::endl;

                // 尝试重新打开摄像头
                if (!reconnectCamera())
                {
                    break;
                }
            }

            // 控制帧率
            auto end_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            constexpr int target_frame_time_ms = 1000 / VIDEO_FPS;

            if (elapsed.count() < target_frame_time_ms)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time_ms - elapsed.count()));
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
                capture_queue_cv.wait(lock, [this]
                {
                    return !capture_frame_queue.empty() || !is_video_capturing.load();
                });

                // 如果退出标志设置且队列为空，则退出
                if (!is_video_capturing.load() && capture_frame_queue.empty())
                {
                    break;
                }

                if (!capture_frame_queue.empty())
                {
                    mat_ptr = capture_frame_queue.front();
                    capture_frame_queue.pop();
                }
            }

            if (mat_ptr && is_video_capturing.load() && frame_encoder_ptr)
            {
                frame_encoder_ptr->encodeFrame(*mat_ptr);
            }
        }
    }

    void startCapture()
    {
        if (!initializeCamera())
        {
            return;
        }

        is_video_capturing.store(true);

        // 启动生产者线程
        std::thread producer_thread(&VideoCaptureManager::captureFrame, this);
        producer_thread.detach();

        // 启动消费者线程
        std::thread consumer_thread(&VideoCaptureManager::consumeFrame, this);
        consumer_thread.detach();

        std::cout << "Video capture started successfully!" << std::endl;
    }

    void stopCapture()
    {
        is_video_capturing.store(false);

        // 通知等待的线程
        capture_queue_cv.notify_all();

        if (capture)
        {
            capture->release();
        }
    }

    void setFrameEncoder(std::unique_ptr<FrameEncoder> encoder)
    {
        frame_encoder_ptr = std::move(encoder);
    }

    FrameEncoder* getFrameEncoder() const
    {
        return frame_encoder_ptr.get();
    }

    void setRegistered(const bool registered)
    {
        is_registered.store(registered);
    }

    bool isRegistered() const
    {
        return is_registered.load();
    }

private:
    std::atomic<bool> is_video_capturing{false};
    std::queue<std::shared_ptr<cv::Mat>> capture_frame_queue;
    mutable std::mutex capture_queue_mutex;
    std::condition_variable capture_queue_cv;

    std::atomic<bool> is_registered{false};
    std::unique_ptr<FrameEncoder> frame_encoder_ptr;

    // 摄像头对象
    std::unique_ptr<cv::VideoCapture> capture;

    bool reconnectCamera()
    {
        std::cout << "Attempting to reconnect camera..." << std::endl;

        if (capture)
        {
            capture->release();
        }

        // 等待一段时间再尝试重连
        std::this_thread::sleep_for(std::chrono::seconds(2));

        return initializeCamera();
    }
};

int main()
{
    VideoCaptureManager manager;
    if (!manager.initializeCamera())
    {
        std::cerr << "Failed to initialize camera" << std::endl;
        return -1;
    }

    // 启动视频捕获
    manager.startCapture();
    std::cout << "Camera capturing started" << std::endl;

    // Sip注册
    const auto sip_register = std::make_unique<SipRegister>(
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

    sip_register->setSipEventCallback([&manager](const int code, const std::string& message)
    {
        switch (code)
        {
        case 200:
            {
                manager.setRegistered(true);
                auto encoder = std::make_unique<FrameEncoder>();
                if (!encoder->prepare())
                {
                    std::cerr << "Failed to prepare video encoder" << std::endl;
                }
                else
                {
                    manager.setFrameEncoder(std::move(encoder));
                    std::cout << "Video encoder prepared successfully" << std::endl;
                }
                break;
            }
        case 201:
            {
                manager.setRegistered(false);
                std::cout << "SIP unregistration completed" << std::endl;
                break;
            }
        case 1000:
            {
                if (const auto encoder = manager.getFrameEncoder())
                {
                    encoder->startStream();
                    std::cout << "Video stream started" << std::endl;
                }
                break;
            }
        case 1001:
            {
                if (const auto encoder = manager.getFrameEncoder())
                {
                    encoder->stopStream();
                    std::cout << "Video stream stopped" << std::endl;
                }
                break;
            }
        default:
            break;
        }
        std::cout << "Response code: " << code << ", message: " << message << std::endl;
    });

    // 保持主线程运行
    std::cout << "System running... Press Ctrl+C to exit." << std::endl;
    std::this_thread::sleep_for(std::chrono::hours(24)); // 24小时后自动退出
    manager.stopCapture();
    return 0;
}
