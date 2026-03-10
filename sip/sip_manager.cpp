//
// Created by pengx on 2026/3/10.
//

#include "sip_manager.hpp"

#include <netinet/in.h>
#include <eXosip2/eX_register.h>

#include "state_code.hpp"
#include "response_sender.hpp"
#include "rtp_sender.hpp"

#define HEARTBEAT_INTERVAL 30 // 心跳间隔

using namespace std::chrono;

SipManager::SipManager(const Sip::SipParameter& parameter) : _logger("SipManager") {
    _sip_context_ptr = std::make_unique<SipContext>(parameter);
    _sip_context_ptr->initialize();

    _register_mgr_ptr = std::make_unique<RegisterManager>(_sip_context_ptr.get());
    _register_mgr_ptr->setStateCallback([this](const int error_code) {
        emit sipStateSignal(error_code, StateCode::toString(error_code));
    });

    _event_dispatcher_ptr = std::make_unique<EventDispatcher>(_sip_context_ptr.get(),
                                                              _register_mgr_ptr.get(),
                                                              this,
                                                              this);

    _stream_manager_ptr = std::make_unique<StreamManager>(_sip_context_ptr.get(), this);

    // sip事件循环
    try {
        _is_sip_loop_running = true;
        _sip_event_thread_ptr = std::make_unique<std::thread>(&SipManager::sip_event_loop, this);
    } catch (const std::exception& e) {
        emit sipStateSignal(3003, StateCode::toString(3003));
        _is_sip_loop_running = false;
        _sip_context_ptr->destroy();
        throw;
    }
    _logger.i("SipManager initialized successfully");
}

/**
 * login - 注册到GB平台
 *
 * 功能：
 * 1. 保存JNI回调引用
 * 2. 如果已注册，先注销再重新注册
 * 3. 发送REGISTER请求（首次无认证，收到401后再带认证重试）
 * 4. 注册成功后自动启动心跳线程
 *
 * 注意：
 * - eXosip上下文和事件循环在构造时已初始化，此处不需要重复初始化
 * - 符合GB28181标准注册流程
 */
void SipManager::login() const {
    _register_mgr_ptr->startRegistration();
}

/**
 * logout - 从GB平台注销（软注销）
 *
 * 功能：
 * 1. 停止心跳线程
 * 2. 停止所有媒体推流/接收
 * 3. 发送注销请求（expires=0）
 * 5. 收到200 OK后回调UI
 *
 * 注意：
 * - 不停止SIP事件循环（保留用于后续注册）
 * - 不销毁eXosip上下文（保留用于后续注册）
 * - 不释放JNI回调（保留用于后续注册）
 * - 用户可能后续再次调用login，所以保留基础设施
 */
void SipManager::logout() const {
    _register_mgr_ptr->stopRegistration();
}

/**
 * shutdown - 完全销毁，释放所有资源
 *
 * 功能：
 * 1. 如果已注册，执行注销（发送注销请求但不等待响应）
 * 2. 停止心跳线程
 * 3. 停止所有媒体推流/接收
 * 4. 停止SIP事件循环
 * 5. 销毁eXosip上下文
 * 6. 释放JNI回调引用
 *
 * 注意：
 * - 此函数在APP退出时调用，会完全清理所有资源
 * - 注销请求发送后不等待响应，给100ms发送时间
 * - 不回调UI（APP正在退出，UI可能已销毁）
 */
void SipManager::shutdown() {
    // 先注销
    logout();

    // 停止事件循环
    stop_sip_event_loop();

    // 销毁 eXosip 上下文
    if (_sip_context_ptr) {
        _sip_context_ptr->destroy();
    }
}

bool SipManager::isRegistered() const {
    return _register_mgr_ptr->isRegistered();
}

SipManager::~SipManager() {
    _logger.i("SipManager 正在析构中...");
    shutdown();
    _logger.i("SipManager 已析构完成");
}

void SipManager::onLoginSuccess() {
    _register_mgr_ptr->setState(Sip::RegisterState::SUCCESS);
    // ============ 启动心跳线程 ============
    if (_heartbeat_manager_ptr && _heartbeat_manager_ptr->isRunning()) {
        _logger.i("停止旧的心跳线程");
        _heartbeat_manager_ptr->stop();
    }

    if (!_heartbeat_manager_ptr) {
        _heartbeat_manager_ptr = std::make_unique<HeartbeatManager>(HEARTBEAT_INTERVAL);
        _heartbeat_manager_ptr->setCallback([this]() -> void {
            const auto rid = _register_mgr_ptr->getRegisterId();
            const auto sn = _sn_counter.fetch_add(1);
            ResponseSender::get()->sendHeartbeatResponse(_sip_context_ptr.get(), rid, sn);
        });
    }
    _heartbeat_manager_ptr->start();

    emit sipStateSignal(1000, StateCode::toString(1000));
}

void SipManager::onLogoutSuccess() {
    if (_heartbeat_manager_ptr) {
        _heartbeat_manager_ptr->stop();
    }

    _stream_manager_ptr->stopPushStream();
    _stream_manager_ptr->stopReceiveAudio();

    // 重置注册状态
    _register_mgr_ptr->setState(Sip::RegisterState::IDLE);
    _stream_manager_ptr->reset();

    // 清理eXosip内部的注册事务
    if (_sip_context_ptr->isValid() && _register_mgr_ptr->getRegisterId() > 0) {
        _sip_context_ptr->lock();
        eXosip_register_remove(_sip_context_ptr->getContextPtr(),
                               _register_mgr_ptr->getRegisterId());
        _sip_context_ptr->unlock();
        _logger.i("已移除eXosip注册记录");
    }

    emit sipStateSignal(201, StateCode::toString(201));
    _logger.iBox().add("注销成功").add("保留eXosip资源，可再次注册").print();
}

void SipManager::onEventError(const int code, const std::string& message) {
    emit sipStateSignal(code, message);
}

void SipManager::onStartPushStream(eXosip_event_t* event) {
    _stream_manager_ptr->handleVideoInvite(event);
}

void SipManager::initAudioReceiver(const std::string& source_id, const std::string& target_id) {
    _stream_manager_ptr->initAudioReceiver(source_id, target_id);
}

void SipManager::onStartReceiveAudio(eXosip_event_t* event) {
    _stream_manager_ptr->handleAudioAnswer(event);
}

void SipManager::onMediaClosed(const int cid) {
    _stream_manager_ptr->callClosed(cid);
}

void SipManager::onG711DataReceived(uint8_t* g711, const size_t len) {
    const QByteArray g711_data(reinterpret_cast<const char*>(g711), static_cast<int>(len));
    emit g711DataSignal(g711_data);
}

void SipManager::onPcmDataReceived(int16_t* pcm, const size_t samples) {
    const std::vector<int16_t> pcm_data(pcm, pcm + samples);
    emit pcmDataSignal(pcm_data);
}

void SipManager::onStreamStateChanged(const int code, const std::string& message) {
    emit sipStateSignal(code, message);
}

// ----------------------------- 私有函数 ----------------------------- //
void SipManager::sip_event_loop() {
    _logger.dBox()
           .add("SIP 事件循环线程已启动")
           .addFmt("线程ID: %zu", std::hash<std::thread::id>{}(std::this_thread::get_id()))
           .print();

    // 用于统计空闲循环次数
    int idle_cycle_count = 0;
    // 记录上次打印日志的时间点
    auto last_heartbeat_log_time = steady_clock::now();

    // 主循环：持续运行直到收到停止信号
    while (_is_sip_loop_running.load()) {
        if (!_sip_context_ptr->isValid()) {
            emit sipStateSignal(1107, StateCode::toString(1107));
            break;
        }

        // 等待Sip事件（阻塞100毫秒）
        eXosip_event_t* event = eXosip_event_wait(_sip_context_ptr->getContextPtr(), 100, 0);
        if (event == nullptr) {
            if (_sip_context_ptr) {
                // 执行 eXosip 内部定时任务——处理重传、超时等内部维护工作
                eXosip_execute(_sip_context_ptr->getContextPtr());
            }
            idle_cycle_count++;

            auto now = steady_clock::now();
            auto elapsed = duration_cast<seconds>(now - last_heartbeat_log_time);

            if (elapsed.count() >= 30) {
                const auto state = _register_mgr_ptr->getState();
                _logger.dBox()
                       .add("SIP 事件循环心跳")
                       .addFmt("空闲循环次数: %d (过去30秒)", idle_cycle_count)
                       .addFmt("当前注册状态: %s", _register_mgr_ptr->toStateString(state).c_str())
                       .print();
                last_heartbeat_log_time = now;
            }
            continue;
        }

        // ============ 收到事件，重置空闲计数并获取响应消息 ============
        idle_cycle_count = 0;
        if (_event_dispatcher_ptr) {
            _event_dispatcher_ptr->dispatchEvent(event);
        }

        /**
         * 重要：必须释放事件，否则会内存泄漏
         * eXosip_event_free 会释放：
         * - event 结构体本身
         * - event->request
         * - event->response
         * - 其他关联资源
         */
        eXosip_event_free(event);
    }
    _logger.dBox()
           .add("SIP 事件循环线程正常退出")
           .addFmt("总空闲循环次数: %d", idle_cycle_count)
           .print();
}

void SipManager::stop_sip_event_loop() {
    std::lock_guard<std::mutex> lock(_event_loop_mutex);

    if (!_is_sip_loop_running.load()) {
        return;
    }

    _logger.i("正在停止 SIP 事件循环...");

    _is_sip_loop_running = false;

    if (_sip_event_thread_ptr && _sip_event_thread_ptr->joinable()) {
        _sip_event_thread_ptr->join();
    }
    _sip_event_thread_ptr.reset();

    _logger.i("SIP 事件循环已停止");
}