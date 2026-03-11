#include <condition_variable>
#include <csignal>
#include <iostream>
#include <opencv2/opencv.hpp>

#include "base_config.hpp"
#include "frame_capture.hpp"
#include "logger.hpp"
#include "ps_muxer.hpp"
#include "sip_manager.hpp"
#include "video/frame_encoder.hpp"

static constexpr int TIMESTAMP_BASE = 90000; // 90kHz

static std::unique_ptr<Logger> logger_ptr = nullptr;
static std::unique_ptr<FrameEncoder> frame_encoder_ptr = nullptr;
static std::unique_ptr<FrameCapture> frame_capture_ptr = nullptr;
static std::unique_ptr<SipManager> sip_manager_ptr = nullptr;

static std::atomic<bool> is_app_running{true};
static std::atomic<bool> is_registered{false};
static std::atomic<bool> is_push_stream{false};
static std::atomic<uint32_t> frame_count{0};

static std::mutex exit_mutex;
static std::condition_variable exit_cv;

// ============================================================
// 信号处理以及程序清理
// ============================================================
void signal_handler(const int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logger_ptr->dBox()
                  .addFmt("received signal %d", signal)
                  .add("cleaning up...")
                  .print();
        is_app_running = false;
        exit_cv.notify_all();
    }
}

void cleanup() {
    logger_ptr->i("Starting cleanup process...");

    is_push_stream = false;

    if (sip_manager_ptr) {
        logger_ptr->i("Stopping SIP manager...");
        sip_manager_ptr->logout();
        sip_manager_ptr->shutdown();
    }

    if (frame_capture_ptr) {
        logger_ptr->i("Stopping frame capture...");
        frame_capture_ptr->stop();
        frame_capture_ptr.reset();
    }

    if (frame_encoder_ptr) {
        logger_ptr->i("Stopping frame encoder...");
        frame_encoder_ptr->stop();
        frame_encoder_ptr.reset();
    }

    logger_ptr->i("Releasing PS muxer...");
    PsMuxer::get()->release();

    logger_ptr->i("Cleanup completed.");
}

// ============================================================
// SIP管理
// ============================================================
static void handle_sip_message(const int code, const std::string& message) {
    logger_ptr->dBox().addFmt("响应码：%d", code).add(message).print();
    if (code == 1000) {
        // 注册成功
        is_registered = true;
    } else if (code == 201) {
        // 注销成功
        is_registered = false;
    } else if (code == 2100) {
        // 开始推流
        is_push_stream = true;
    } else if (code == 2101) {
        // 停止推流
        is_push_stream = false;
    } else if (code == 2200) {
        // 开始播放对讲语音
    } else
        if (code == 2201) {}
}

static void play_audio_in_pcm(int16_t* pcm, const size_t samples) {
    std::cout << "Playing audio in PCM" << std::endl;
}

static void play_audio_in_g711(uint8_t* g711, const size_t samples) {
    std::cout << "Playing audio in G711" << std::endl;
}

// ============================================================
// 主进程
// ============================================================
int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    logger_ptr = std::make_unique<Logger>("main");

    frame_encoder_ptr = std::make_unique<FrameEncoder>(VIDEO_FPS);
    frame_encoder_ptr->start([](const std::vector<uint8_t>& h264) {
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
    logger_ptr->i("Frame encoder started");

    // 摄像头采集
    frame_capture_ptr = std::make_unique<FrameCapture>(0);
    frame_capture_ptr->setCameraCallback([](const cv::Mat& frame) {
        if (frame_encoder_ptr) {
            frame_encoder_ptr->pushFrame(frame);
        }
    });
    if (!frame_capture_ptr->start()) {
        logger_ptr->e("Cannot open camera, application exit");
        if (frame_encoder_ptr) {
            logger_ptr->i("Stopping frame encoder...");
            frame_encoder_ptr->stop();
            frame_encoder_ptr.reset();
        }
        return 0;
    }
    logger_ptr->i("Camera capturing started");

    // Sip注册
    Sip::SipParameter param;
    param.localHost = "192.168.3.131";
    param.serverHost = "111.198.10.15";
    param.serverPort = 22117;
    param.serverCode = "11010800002000000002";
    param.serverDomain = "1101080000";
    param.deviceCode = "11010800001300011118";
    param.serialNumber = "";
    param.deviceName = "L1300011118";
    param.password = "1234qwer";
    param.longitude = 116.3975;
    param.latitude = 39.9085;
    sip_manager_ptr = std::make_unique<SipManager>(param,
                                                   [](const int code, const std::string& message) {
                                                       handle_sip_message(code, message);
                                                   },
                                                   [](int16_t* pcm, const size_t len) {
                                                       play_audio_in_pcm(pcm, len);
                                                   },
                                                   [](uint8_t* g711, const size_t len) {
                                                       play_audio_in_g711(g711, len);
                                                   });
    sip_manager_ptr->login();
    logger_ptr->i("System running... Press Ctrl+C to exit.");

    // 等待退出信号
    std::unique_lock<std::mutex> lock(exit_mutex);
    exit_cv.wait(lock, [] {
        return !is_app_running.load();
    });
    cleanup();
    return 0;
}
