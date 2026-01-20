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

struct SdpStruct; // 前向声明 SdpStruct

class SipRegister {
public:
    // 状态枚举
    enum RegisterState {
        REG_STATE_IDLE,
        REG_STATE_SENT_INITIAL,
        REG_STATE_SENT_AUTH,
        REG_STATE_SUCCESS,
        REG_STATE_FAILED
    };

    // 回调类型
    using SipEventCallback = std::function<void(int, const std::string &)>;

    // 构造函数
    explicit SipRegister(const std::string &local_host,
                         const std::string &server_host,
                         int server_port,
                         const std::string &server_code,
                         const std::string &server_domain,
                         const std::string &device_code,
                         const std::string &serial_number,
                         const std::string &device_name,
                         const std::string &password,
                         double longitude,
                         double latitude);

    // 设置回调函数
    void setSipEventCallback(const SipEventCallback &callback);

    // 注销
    void deregistration();

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
    SipEventCallback _sip_event_callback = nullptr;

    /**
     * sip事件循环
     * */
    void sip_event_loop();

    // ================================ SipRegister内部变量 ================================ //
    int _reg_id = -1;
    std::atomic<RegisterState> _reg_state{REG_STATE_IDLE};
    std::atomic<bool> _is_do_unregister{false};
    std::atomic<bool> _is_heartbeat_thread_running{false};
    std::unique_ptr<std::thread> _heartbeat_thread_ptr = nullptr;
    std::atomic<int> _sn_cache{1};

    // 常量定义
    static constexpr int DEFAULT_SIP_PORT = 5060;
    static constexpr int REGISTER_EXPIRED_TIME = 3600;    // 1小时
    static constexpr int HEARTBEAT_INTERVAL_SECONDS = 60; // 60秒
    static constexpr int EVENT_LOG_INTERVAL_SECONDS = 10; // 日志间隔

    /**
     * 注册成功
     */
    void register_success();

    /**
     * Sip心跳
     */
    void heartbeat();

    /**
     * @brief 注册失败处理
     * @param response 失败响应消息
     */
    void register_failed(const osip_message_t *response);

    /**
     * @brief 处理来自平台的MESSAGE事件
     * @param event eXosip事件
     */
    void platform_event(const eXosip_event_t *event);

    /**
     * @brief 处理来自平台的INVITE事件，开始推流
     * @param event eXosip事件
    */
    void start_push_stream(const eXosip_event_t *event);
};

#endif //SIP_REGISTER_HPP
