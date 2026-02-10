#include <csignal>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <condition_variable>

#include "frame_capture.hpp"
#include "frame_encoder.hpp"
#include "sip_register.hpp"

static FrameCapture* frame_capture_ptr = nullptr;
static SipRegister* sip_register_ptr = nullptr;
static std::atomic<bool> is_app_running{true};
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
    std::cout << "Received camera frame: " << frame.cols << "x" << frame.rows << std::endl;
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
        std::cout << "Stopping camera capturing" << std::endl;
        frame_capture_ptr->stop();
        delete frame_capture_ptr;
        frame_capture_ptr = nullptr;
    }

    // if (_captureThreadPtr) {
    //     _captureThreadPtr->quit();
    //     _captureThreadPtr->wait();
    //     delete _captureThreadPtr;
    // }
    // delete _frameCapturePtr;
    //
    // if (_encodeThreadPtr) {
    //     _encodeThreadPtr->quit();
    //     _encodeThreadPtr->wait();
    //     delete _encodeThreadPtr;
    // }
    // delete _frameEncoderPtr;

    std::cout << "Cleanup completed." << std::endl;
}

int main() {
    signal(SIGINT, signal_handler);

    // 设置输出缓冲，确保能立即看到输出
    std::cout << std::unitbuf;

    // 摄像头采集
    frame_capture_ptr = new FrameCapture(0,
                                         [](const std::string& error) {
                                             handle_camera_error(error);
                                         },
                                         [](const cv::Mat& frame) {
                                             handle_camera_frame(frame);
                                         });
    frame_capture_ptr->start();
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
