//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_SIP_MANAGER_HPP
#define GB28181CONSOLE_SIP_MANAGER_HPP

#include <mutex>
#include <thread>
#include <eXosip2/eX_setup.h>

#include "event_dispatcher.hpp"
#include "heartbeat_manager.hpp"
#include "logger.hpp"
#include "register_manager.hpp"
#include "sdp_parser.hpp"
#include "sip.hpp"
#include "sip_context.hpp"
#include "stream_manager.hpp"

class SipManager : public IEventObserver, public IMediaObserver, public IStreamObserver {
public:
    using SipStateCallback = std::function<void(int, const std::string&)>;

    using PcmDataCallback = std::function<void(int16_t*, size_t)>;

    using G711DataCallback = std::function<void(uint8_t*, size_t)>;

    /**
     * 初始化 eXosip 栈
     */
    explicit SipManager(const Sip::SipParameter& parameter, const SipStateCallback& sip_state_callback,
                        const PcmDataCallback& pcm_data_callback, const G711DataCallback& g711_data_callback);

    void login() const;

    void logout() const;

    void shutdown();

    bool isRegistered() const;

    ~SipManager() override;

    // ============================================================
    // IEventObserver 接口实现
    // ============================================================
    void onLoginSuccess() override;

    void onLogoutSuccess() override;

    void onEventError(int code, const std::string& message) override;

    // ============================================================
    // IMediaObserver 接口实现
    // ============================================================
    void onStartPushStream(eXosip_event_t* event) override;

    void initAudioReceiver(const std::string& source_id, const std::string& target_id) override;

    void onStartReceiveAudio(eXosip_event_t* event) override;

    void onMediaClosed(int cid) override;

    // ============================================================
    // IStreamObserver 接口实现
    // ============================================================
    void onPcmDataReceived(int16_t* pcm, size_t samples) override;

    void onG711DataReceived(uint8_t* g711, size_t len) override;

    void onStreamStateChanged(int code, const std::string& message) override;

private:
    Logger _logger;

    std::unique_ptr<SipContext> _sip_context_ptr = nullptr;
    std::unique_ptr<RegisterManager> _register_mgr_ptr = nullptr;
    std::unique_ptr<HeartbeatManager> _heartbeat_manager_ptr = nullptr;
    std::unique_ptr<EventDispatcher> _event_dispatcher_ptr = nullptr;
    std::unique_ptr<StreamManager> _stream_manager_ptr = nullptr;

    // sip事件循环
    std::atomic<bool> _is_sip_loop_running{false};
    std::unique_ptr<std::thread> _sip_event_thread_ptr = nullptr;
    std::mutex _event_loop_mutex;

    // 通道编号
    std::atomic<int> _sn_counter{1};

    SipStateCallback _sip_state_callback = nullptr;
    PcmDataCallback _pcm_data_callback = nullptr;
    G711DataCallback _g711_data_callback = nullptr;

    /**
     * sip事件循环
     * */
    void sip_event_loop();

    /**
     * 停止sip事件循环
     * */
    void stop_sip_event_loop();
};

#endif //GB28181CONSOLE_SIP_MANAGER_HPP
