//
// Created by pengx on 2026/2/10.
//

#ifndef GB28181CONSOLE_AUDIO_RECEIVER_HPP
#define GB28181CONSOLE_AUDIO_RECEIVER_HPP

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "ring_buffer.hpp"

class AudioReceiver {
public:
    ~AudioReceiver();

    using AudioDataCallback = std::function<void(uint8_t* buffer, size_t len)>;

    int initialize();

    bool connectPlatform(const std::string& server_ip, uint16_t server_port) const;

    void start(AudioDataCallback callback);

    void stop();

private:
    int _receive_socket_fd = -1;
    std::atomic<bool> _is_thread_running{false};
    std::thread _receive_thread;
    AudioDataCallback _audio_callback;

    // 环形缓冲区（256KB，可存储约1.6秒的PCMA数据@128kbps）
    RingBuffer _ring_buffer{256 * 1024};

    // G.711帧处理缓冲区（每帧160字节，20ms）
    std::vector<uint8_t> _frame_buffer;
    std::atomic<int> _frame_count{0};

    void data_receive_loop();

    // 处理音频帧
    void handle_audio_frames();
};

#endif //GB28181CONSOLE_AUDIO_RECEIVER_HPP