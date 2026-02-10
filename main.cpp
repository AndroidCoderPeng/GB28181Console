#include <csignal>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <condition_variable>

#include "frame_capture.hpp"
#include "frame_encoder.hpp"
#include "sip_register.hpp"
#include "base_config.hpp"
#include "ps_muxer.hpp"

static FrameCapture* frame_capture_ptr = nullptr;
static std::thread* capture_thread_ptr = nullptr;
static FrameEncoder* frame_encoder_ptr = nullptr;
static SipRegister* sip_register_ptr = nullptr;
static std::atomic<bool> is_app_running{true};
static std::atomic<bool> is_push_stream{false};
static std::atomic<uint32_t> frame_count{0};
static constexpr int TIMESTAMP_BASE = 90000; // 90kHz
static std::mutex exit_mutex;
static std::condition_variable exit_cv;

// 前向声明清理函数
void cleanup();

void signal_handler(const int signal) {
    if (signal == SIGINT) {
        std::cout << "Received SIGINT, cleaning up..." << std::endl;
        is_app_running = false;
        exit_cv.notify_all();
    }
}

static void handle_camera_error(const std::string& error) {
    std::cerr << "Camera error: " << error << std::endl;
}

static void handle_camera_frame(const cv::Mat& frame) {
    // / 将帧推入编码器的环形缓冲区并触发编码
    if (frame_encoder_ptr) {
        frame_encoder_ptr->pushFrame(frame);
    }
}

static void handle_sip_message(const int code, const std::string& message) {
    std::cout << "Response code: " << code << ", " << message << std::endl;
    switch (code) {
        case 200:
        {
            std::cout << "SIP registration completed" << std::endl;
            break;
        }
        case 201:
        {
            std::cout << "SIP unregistration completed" << std::endl;
            break;
        }
        case 1000:
        {
            std::cout << "Video stream started" << std::endl;
            break;
        }
        case 1001:
        {
            std::cout << "Video stream stopped" << std::endl;
            break;
        }
        default:
            break;
    }
}

static void play_audio_in_pcm(const std::vector<int16_t> pcm, const size_t samples) {
    std::cout << "Playing audio in PCM" << std::endl;
}

static void play_audio_in_g711(const std::vector<int8_t> g711, const size_t samples) {
    std::cout << "Playing audio in G711" << std::endl;
}

void cleanup() {
    if (sip_register_ptr) {
        sip_register_ptr->unRegister();
    }

    if (frame_capture_ptr) {
        frame_capture_ptr->stop();
    }
    if (capture_thread_ptr && capture_thread_ptr->joinable()) {
        capture_thread_ptr->join();
        delete capture_thread_ptr;
    }
    delete frame_capture_ptr;

    if (frame_encoder_ptr) {
        frame_encoder_ptr->stop();
        delete frame_encoder_ptr;
    }

    std::cout << "Cleanup completed." << std::endl;
}

int main() {
    signal(SIGINT, signal_handler);

    // 设置输出缓冲，确保能立即看到输出
    std::cout << std::unitbuf;

    frame_encoder_ptr = new FrameEncoder(VIDEO_FPS);
    frame_encoder_ptr->setH264DataCallback([](const std::vector<uint8_t>& h264) {
        if (h264.empty()) {
            return;
        }
        if (is_push_stream.load()) {
            // 计算 90kHz 时间戳（固定帧率）
            // 每帧间隔 = 90000 / 25 = 3600
            const uint32_t pts_90k = frame_count * (TIMESTAMP_BASE / VIDEO_FPS);

            PsMuxer::get()->writeVideoFrame(h264.data(), pts_90k, h264.size());
            frame_count.fetch_add(1, std::memory_order_relaxed);
        }
    });
    frame_encoder_ptr->start();
    std::cout << "Frame encoder started" << std::endl;

    // 摄像头采集
    frame_capture_ptr = new FrameCapture(0,
                                         [](const std::string& error) {
                                             handle_camera_error(error);
                                         },
                                         [](const cv::Mat& frame) {
                                             handle_camera_frame(frame);
                                         });
    capture_thread_ptr = new std::thread(&FrameCapture::start, frame_capture_ptr);
    std::cout << "Camera capturing started" << std::endl;

    // Sip注册
    sip_register_ptr = new SipRegister("192.168.3.131",
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
    sip_register_ptr->doRegister([](const int code, const std::string& message) {
                                     handle_sip_message(code, message);
                                 },
                                 [](const std::vector<int16_t>& pcm, const size_t samples) {
                                     play_audio_in_pcm(pcm, samples);
                                 },
                                 [](const std::vector<int8_t>& g711, const size_t samples) {
                                     play_audio_in_g711(g711, samples);
                                 });

    // 保持主线程运行
    std::cout << "System running... Press Ctrl+C to exit." << std::endl;

    // 等待退出信号
    {
        std::unique_lock<std::mutex> lock(exit_mutex);
        exit_cv.wait(lock, [] {
            return !is_app_running.load();
        });
    }

    // 执行清理
    cleanup();

    std::cout << "System exited successfully." << std::endl;
    return 0;
}
