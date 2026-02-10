//
// Created by pengx on 2025/9/29.
//

#ifndef SIP_REGISTER_HPP
#define SIP_REGISTER_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <eXosip2/eX_setup.h>
#include "pugixml.hpp"

#include "sdp_parser.hpp"
#include "audio_receiver.hpp"

class SipRegister {
public:
    using SipEventCallback = std::function<void(int, const std::string&)>;
    using PcmDataCallback = std::function<void(std::vector<int16_t>, size_t)>;
    using G711DataCallback = std::function<void(std::vector<int8_t>, size_t)>;

    // 构造函数
    explicit SipRegister(const std::string& local_host, const std::string& server_host, int server_port,
                         const std::string& server_code, const std::string& server_domain,
                         const std::string& device_code, const std::string& serial_number,
                         const std::string& device_name, const std::string& password,
                         double longitude, double latitude);

    // 设置回调函数
    void setCallback(const SipEventCallback& event_callback,
                     const PcmDataCallback& pcm_callback,
                     const G711DataCallback& g711_callback);

    /**
     * 注册
     */
    bool doRegister();

    /**
     * 注销（发送 expires=0 的 REGISTER）
     */
    bool unRegister();

    ~SipRegister();

private:
    // 状态枚举
    enum RegisterState {
        REG_STATE_IDLE,
        REG_STATE_SENT_INITIAL,
        REG_STATE_SENT_AUTH,
        REG_STATE_SUCCESS,
        REG_STATE_FAILED
    };

    // sip注册参数
    std::string _local_host;
    std::string _server_host;
    uint16_t _server_port;
    std::string _server_code;
    std::string _server_domain;
    std::string _device_code;
    std::string _serial_number;
    std::string _device_name;
    std::string _password;
    double _longitude;
    double _latitude;

    // sip相关
    eXosip_t* _ex_context_ptr = nullptr;
    std::atomic<bool> _is_sip_loop_running{false};
    std::unique_ptr<std::thread> _sip_event_thread_ptr = nullptr;
    std::mutex _event_loop_mutex;

    // uri相关
    std::string _from_uri;
    std::string _to_uri;
    std::string _proxy_uri;

    // 注册相关
    std::atomic<int> _reg_id{-1};
    std::atomic<RegisterState> _reg_state{REG_STATE_IDLE};
    std::atomic<bool> _is_do_unregister{false};
    std::mutex _register_mutex;

    // 心跳相关
    std::atomic<bool> _is_heartbeat_thread_running{false};
    std::unique_ptr<std::thread> _heartbeat_thread_ptr = nullptr;
    std::mutex _heartbeat_mutex;

    // 通道编号
    std::atomic<int> _sn_counter{0};

    // 视频推流相关
    std::atomic<int> _video_call_id{-1};
    std::atomic<int> _video_dialog_id{-1};

    // 语音对讲相关
    std::atomic<int> _audio_call_id{-1};
    std::atomic<int> _audio_dialog_id{-1};

    std::mutex _audio_mutex;
    std::shared_ptr<AudioReceiver> _audio_receiver_ptr = nullptr; // 智能指针，无需手动析构

    // 回调函数
    SipEventCallback _event_callback;
    PcmDataCallback _pcm_callback;
    G711DataCallback _g711_callback;

    /**
     * sip事件循环
     * */
    void sip_event_loop();

    /**
     * 停止sip事件循环
     * */
    void stop_sip_event_loop();

    /**
     * 启动sip心跳
     * */
    bool start_heartbeat_thread();

    /**
     * 停止sip心跳
     * */
    void stop_heartbeat_thread();

    /**
     * 分发Sip事件
     * */
    void dispatch_sip_event(const eXosip_event_t* event);

    void start_push_stream(const eXosip_event_t* event);

    /**
     * 向平台发送呼叫相关的错误响应
     *
     * @param tid 事务ID
     * @param status_code 状态码
     * @param reason 原因短语
     * @return true=成功, false=失败
     */
    bool send_call_error_response(int tid, int status_code, const std::string& reason) const;

    bool stop_push_stream();

    /**
     * 注册/注销
     * @param expires 0:注销，其他：注册
     * */
    bool real_sip_registration(uint16_t expires);

    /**
     * 带有鉴权的注册
     * @return
     */
    bool sip_registration_with_auth();

    /**
     * 注册成功
     */
    void register_success();

    /**
     * Sip心跳
     */
    void heartbeat();

    void platform_event(const eXosip_event_t* event);

    /**
     * 解析并回复查询类信令
     * */
    void response_query_sip(const pugi::xml_document& xml);

    /**
     * 回复平台消息
     * */
    bool send_message_request(const std::string& response) const;

    /**
     * 向平台发送错误响应
     *
     * @param tid 事务ID
     * @param status_code 状态码（400/415/500等）
     * @param reason 原因短语
     * @return true=成功, false=失败
     */
    bool send_error_response(int tid, int status_code, const std::string& reason) const;

    /**
     * 解析并回复通知类信令，如：语音对讲
     * */
    void response_notify_sip(const pugi::xml_document& xml);

    /**
     * 在音频下行场景中，设备需要主动向平台发送INVITE，请求平台推送音频流
     * - From = 设备自己（发起请求的一方），设备在平台SIP域内的注册地址（与注册时保持一致）
     * - To = 平台（接收请求的一方），平台在其SIP域内的地址
     * */
    bool send_audio_invite(const std::string& source_id, const std::string& target_id, uint16_t local_port);

    void start_receive_audio(const eXosip_event_t* event);

    bool stop_receive_audio();

    std::string current_register_state() const;
};

#endif //SIP_REGISTER_HPP
