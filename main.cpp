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
static std::atomic<bool> is_audio_talking{false};
static std::atomic<uint32_t> frame_count{0};

static std::mutex exit_mutex;
static std::condition_variable exit_cv;

// 环形缓冲区（256KB，可存储约1.6秒的PCMA数据@128kbps）
static RingBuffer audio_buffer{256 * 1024};

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
// 音频对讲
// ============================================================
static void play_audio_talk() {
    if (!is_audio_talking) {
        return;
    }

    // 从环形缓冲区读取数据并播放
    constexpr size_t chunkSize = 1024; // 每次读取1KB数据
    std::vector<uint8_t> audioData(chunkSize);

    const size_t bytesRead = audio_buffer.read(audioData.data(), chunkSize);
    if (bytesRead > 0) {
        // _audioDevicePtr->write(reinterpret_cast<const char*>(audioData.data()),
        //                        static_cast<qint64>(bytesRead));
    }

    // 如果没有数据可读，但仍在播放状态，继续定时检查
    if (is_audio_talking) {
        // 10ms后继续检查
        // QTimer::singleShot(10, this, &MainWindow::playAudioTalk);
    }
}

static void stop_audio_talk() {
    is_audio_talking = false;

    // if (_audioOutputPtr) {
    //     _audioOutputPtr->stop();
    //     delete _audioOutputPtr;
    //     _audioOutputPtr = nullptr;
    //     _audioDevicePtr = nullptr;
    // }

    audio_buffer.clear();
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
        if (is_audio_talking) {
            stop_audio_talk();
        }

        // 设置播放状态
        is_audio_talking = true;

        // 音频对讲初始化
        // QAudioFormat format;
        // format.setSampleRate(8000);
        // format.setChannelCount(1);
        // format.setSampleSize(16);
        // format.setCodec("audio/pcm");
        // format.setByteOrder(QAudioFormat::LittleEndian);
        // format.setSampleType(QAudioFormat::SignedInt);
        // _audioOutputPtr = new QAudioOutput(format);
        // _audioDevicePtr = _audioOutputPtr->start();

        // 开始播放循环
        play_audio_talk();
    } else if (code == 2201) {
        stop_audio_talk();
    }
}

static void play_audio_in_pcm(int16_t* pcm, const size_t samples) {
    std::cout << "Playing audio in PCM" << std::endl;
    // 直接播放，无需转换
    // 设置音频格式：8000Hz、单声道、16-bit
    if (samples == 0) {
        return;
    }

    const std::vector<int16_t> pcm_data(pcm, pcm + samples);

    // 将PCM数据转换为字节数组
    const auto data = reinterpret_cast<const uint8_t*>(pcm_data.data());
    const size_t size = pcm_data.size() * sizeof(int16_t);

    // 检查缓冲区是否有足够空间
    if (size > audio_buffer.writable_size()) {
        // 丢弃的数据量 = 新数据大小 - 当前可写空间
        const size_t discardSize = size - audio_buffer.writable_size();
        audio_buffer.discard(discardSize);
    }

    // 写入数据到环形缓冲区
    audio_buffer.write(data, size);
}

static void play_audio_in_g711(uint8_t* g711, const size_t samples, const int type) {
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
                                                   [](uint8_t* g711, const size_t len, const int type) {
                                                       play_audio_in_g711(g711, len, type);
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
