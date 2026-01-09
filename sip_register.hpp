//
// Created by pengx on 2025/9/29.
//

#ifndef SIP_REGISTER_HPP
#define SIP_REGISTER_HPP

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <eXosip2/eX_setup.h>

#include "sdp_parser.hpp"

typedef std::function<void(int, const std::string &)> sip_event_callback;

class SipRegister {
public:
    enum RegisterState {
        REG_STATE_IDLE,
        REG_STATE_SENT_INITIAL,
        REG_STATE_SENT_AUTH,
        REG_STATE_SUCCESS,
        REG_STATE_FAILED
    };

    SipRegister(const std::string &local_host, const std::string &server_host, int server_port,
                const std::string &server_code, const std::string &server_domain, const std::string &device_code,
                const std::string &serial_number, const std::string &device_name, const std::string &password,
                double longitude, double latitude);

    void sipEventCallback(const sip_event_callback &callback);

    void unregister();

    ~SipRegister();

private:
    // ================================ Sip注册参数 ================================ //
    std::string _local_host;
    std::string _server_host;
    int _server_port{};
    std::string _server_code;
    std::string _server_domain;
    std::string _device_code;
    std::string _serial_number;
    std::string _device_name;
    std::string _password;
    double _longitude{};
    double _latitude{};

    // ================================ Sip内部事件 ================================ //
    eXosip_t *_ex_context_ptr{};
    std::atomic<bool> _is_sip_loop_running{false};
    std::unique_ptr<std::thread> _sip_event_thread = nullptr;
    sip_event_callback _sip_event_callback = nullptr;

    /**
     * sip事件循环
     * */
    void sip_event_loop();

    // ================================ SipRegister内部变量 ================================ //
    int _reg_id = -1;
    RegisterState _reg_state = REG_STATE_IDLE;
    bool _is_do_unregister = false;
    std::atomic<bool> _is_heartbeat_thread_running{false};
    std::unique_ptr<std::thread> _heartbeat_thread_ptr = nullptr;
    std::atomic<int> _sn_cache{1};

    /**
     * 注册成功
     */
    void register_success();

    /**
     * Sip心跳
     */
    void heartbeat();

    void register_failed(const osip_message_t *response);

    void platform_event(const eXosip_event_t *event);

    void start_push_stream(const eXosip_event_t *event);
};

#endif //SIP_REGISTER_HPP
