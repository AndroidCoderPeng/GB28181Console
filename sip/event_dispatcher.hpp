//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_EVENT_DISPATCHER_HPP
#define GB28181CONSOLE_EVENT_DISPATCHER_HPP

#include <eXosip2/eX_setup.h>

#include "logger.hpp"
#include "pugixml.hpp"

// 前向声明
class SipContext;

class RegisterManager;

class IEventObserver {
public:
    virtual ~IEventObserver() = default;

    virtual void onLoginSuccess() = 0;

    virtual void onLogoutSuccess() = 0;

    virtual void onEventError(int code, const std::string& message) = 0;
};

class IMediaObserver {
public:
    virtual ~IMediaObserver() = default;

    virtual void onStartPushStream(eXosip_event_t* event) = 0;

    virtual void initAudioReceiver(const std::string& source_id, const std::string& target_id) = 0;

    virtual void onStartReceiveAudio(eXosip_event_t* event) = 0;

    virtual void onMediaClosed(int cid) = 0;
};

class EventDispatcher {
public:
    /**
     * 构造函数
     * @param sip_context
     * @param register_mgr
     * @param event_observer
     * @param media_observer
     */
    explicit EventDispatcher(SipContext* sip_context, RegisterManager* register_mgr,
                             IEventObserver* event_observer, IMediaObserver* media_observer);

    /**
     * 分发SIP事件
     * @param event eXosip事件
     */
    void dispatchEvent(eXosip_event_t* event);

private:
    Logger _logger;
    SipContext* _sip_context_ptr;
    RegisterManager* _register_mgr_ptr;
    IEventObserver* _event_observer_ptr;
    IMediaObserver* _media_observer_ptr;

    // ============================================================
    // 事件处理方法
    // ============================================================
    void handle_registration_success(const eXosip_event_t* event);

    void handle_registration_failure(const eXosip_event_t* event);

    void handle_message_new(const eXosip_event_t* event);

    void handle_message_answered(const eXosip_event_t* event);

    void handle_message_request_failure(const eXosip_event_t* event);

    void handle_call_invite(eXosip_event_t* event);

    void handle_call_answered(eXosip_event_t* event);

    void handle_call_ack(const eXosip_event_t* event);

    void handle_call_closed(const eXosip_event_t* event);

    void handle_call_released(const eXosip_event_t* event);

    void handle_call_no_answer(const eXosip_event_t* event);

    void handle_call_cancelled(const eXosip_event_t* event);

    void handle_call_request_failure(const eXosip_event_t* event);

    void handle_call_server_failure(const eXosip_event_t* event);

    void handle_call_global_failure(const eXosip_event_t* event);

    void handle_subscription_events(const eXosip_event_t* event);

    void handle_in_subscription_new() const;

    void handle_notification_events(const eXosip_event_t* event);

    void handle_unknown_event(const eXosip_event_t* event);

    // ============================================================
    // 消息处理相关
    // ============================================================
    void process_message_event(const eXosip_event_t* event);

    void process_query_message(const pugi::xml_document& xml);

    void process_notify_message(const pugi::xml_document& xml);

    // ============================================================
    // 给平台发送消息的相关函数
    // ============================================================
    void send_event_error_response(int tid, int code, const std::string& reason) const;
};

#endif //GB28181CONSOLE_EVENT_DISPATCHER_HPP