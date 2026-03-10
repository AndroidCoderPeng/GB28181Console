//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_RESPONSE_SENDER_HPP
#define GB28181CONSOLE_RESPONSE_SENDER_HPP

#include <functional>
#include <string>

#include "logger.hpp"
#include "sip/sip_context.hpp"

class ResponseSender {
public:
    explicit ResponseSender();

    static ResponseSender* get() {
        static ResponseSender instance;
        return &instance;
    }

    ResponseSender(const ResponseSender&) = delete;

    ResponseSender& operator=(const ResponseSender&) = delete;

    using AudioInviteCallback = std::function<void(int code, const std::string& message)>;

    bool sendEventResponse(const SipContext* context, const std::string& response) const;

    void sendEventErrorResponse(const SipContext* context, int tid, int code, const std::string& reason);

    bool sendHeartbeatResponse(const SipContext* context, int rid, int sn);

    /**
     * 在音频下行场景中，设备需要主动向平台发送INVITE，请求平台推送音频流
     * - From = 设备自己（发起请求的一方），设备在平台SIP域内的注册地址（与注册时保持一致）
     * - To = 平台（接收请求的一方），平台在其SIP域内的地址
     * */
    int sendAudioInvite(const SipContext* context, const std::string& sid, const std::string& tid,
                        uint16_t port, const AudioInviteCallback& callback);

    /**
     * 向平台发送呼叫相关的错误响应
     *
     * @param context
     * @param tid 事务ID
     * @param code 状态码
     * @param reason 原因短语
     * @return true=成功, false=失败
     */
    bool sendCallErrorResponse(const SipContext* context, int tid, int code, const std::string& reason);

private:
    Logger _logger;
};

#endif //GB28181CONSOLE_RESPONSE_SENDER_HPP