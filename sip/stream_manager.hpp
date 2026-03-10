//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_STREAM_MANAGER_HPP
#define GB28181CONSOLE_STREAM_MANAGER_HPP

#include <mutex>

#include "audio/audio_receiver.hpp"
#include "sip_context.hpp"

class IStreamObserver {
public:
    virtual ~IStreamObserver() = default;

    virtual void onPcmDataReceived(int16_t* pcm, size_t samples) = 0;

    virtual void onG711DataReceived(uint8_t* g711, size_t len) = 0;

    virtual void onStreamStateChanged(int code, const std::string& message) = 0;
};

class StreamManager {
public:
    explicit StreamManager(SipContext* context, IStreamObserver* observer);

    // 视频推流相关
    void handleVideoInvite(eXosip_event_t* event);

    bool stopPushStream();

    bool isPushing() const {
        return _video_call_id > 0;
    };

    // 音频接收相关
    void initAudioReceiver(const std::string& source_id, const std::string& target_id);

    void handleAudioAnswer(const eXosip_event_t* event);

    bool stopReceiveAudio();

    bool isReceiving() const {
        return _audio_call_id > 0;
    };

    // 会话管理
    void callClosed(int cid);

    // 重置
    void reset();

private:
    Logger _logger;
    SipContext* _sip_context_ptr;
    IStreamObserver* _stream_observer_ptr;

    std::atomic<int> _video_call_id{-1};
    std::atomic<int> _video_dialog_id{-1};
    std::atomic<int> _audio_call_id{-1};
    std::atomic<int> _audio_dialog_id{-1};

    std::mutex _audio_mutex;
    std::unique_ptr<AudioReceiver> _audio_receiver_ptr;

    // ============================================================
    // 给平台发送消息的相关函数
    // ============================================================
    bool send_sip_call_error_response(int tid, int code, const std::string& reason) const;
};

#endif //GB28181CONSOLE_STREAM_MANAGER_HPP