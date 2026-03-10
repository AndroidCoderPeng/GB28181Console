//
// Created by pengx on 2026/3/10.
//

#include "response_sender.hpp"

#include "sdp_parser.hpp"
#include "state_code.hpp"
#include "xml_builder.hpp"

ResponseSender::ResponseSender() : _logger("ResponseSender") {
    _logger.i("ResponseSender created");
}

bool ResponseSender::sendEventResponse(const SipContext* context, const std::string& response) const {
    if (response.empty()) {
        _logger.e("消息体为空");
        return false;
    }

    if (!context->isValid()) {
        _logger.e("eXosip 上下文为空");
        return false;
    }

    context->lock();

    osip_message_t* msg = nullptr;
    eXosip_message_build_request(context->getContextPtr(),
                                 &msg,
                                 "MESSAGE",
                                 context->getToUri().c_str(),
                                 context->getFromUri().c_str(),
                                 context->getProxyUri().c_str());
    /**
     * GB28181 规定：
     * Content-Type 必须为 "Application/MANSCDP+xml"
     * 注意大小写：Application (首字母大写)
     */
    osip_message_set_content_type(msg, "Application/MANSCDP+xml");
    osip_message_set_body(msg, response.c_str(), response.length());
    eXosip_message_send_request(context->getContextPtr(), msg);
    context->unlock();
    _logger.i("MESSAGE 已发送");
    return true;
}

void ResponseSender::sendEventErrorResponse(const SipContext* context, const int tid, const int code,
                                            const std::string& reason) {
    if (!context->isValid()) {
        _logger.e("eXosip 上下文为空");
        return;
    }

    if (tid <= 0) {
        _logger.eFmt("无效的事务ID: %d", tid);
        return;
    }

    _logger.wFmt("发送事件错误响应: %d %s", code, reason.c_str());

    context->lock();

    // 构建错误响应
    osip_message_t* error_response = nullptr;
    eXosip_message_build_answer(context->getContextPtr(), tid, code, &error_response);
    if (!reason.empty()) {
        if (error_response->reason_phrase) {
            osip_free(error_response->reason_phrase)
        }
        error_response->reason_phrase = osip_strdup(reason.c_str());
    }

    // 发送响应
    eXosip_message_send_answer(context->getContextPtr(), tid, code, error_response);
    context->unlock();
    _logger.dFmt("事件错误响应已发送: %d %s", code, reason.c_str());
}

bool ResponseSender::sendHeartbeatResponse(const SipContext* context, const int rid, const int sn) {
    if (!context->isValid()) {
        _logger.e("eXosip 上下文为空，无法发送心跳");
        return false;
    }

    if (rid <= 0) {
        _logger.wFmt("未注册 (reg_id=%d)，跳过本次心跳", rid);
        return false;
    }

    context->lock();

    osip_message_t* msg = nullptr;
    eXosip_message_build_request(context->getContextPtr(),
                                 &msg,
                                 "MESSAGE",
                                 context->getToUri().c_str(),
                                 context->getFromUri().c_str(),
                                 context->getProxyUri().c_str());
    osip_message_set_content_type(msg, "Application/MANSCDP+xml");

    const std::string heartbeat_xml = XmlBuilder::get()->buildHeartbeat(std::to_string(sn),
                                                                        context->getSipParameter().deviceCode);
    osip_message_set_body(msg, heartbeat_xml.c_str(), heartbeat_xml.length());

    // 发送心跳
    eXosip_message_send_request(context->getContextPtr(), msg);
    context->unlock();
    _logger.dBox()
           .add("心跳已发送")
           .addFmt("register id: %d", rid)
           .addFmt("From: %s", context->getFromUri().c_str())
           .addFmt("To: %s", context->getToUri().c_str())
           .print();
    return true;
}

int ResponseSender::sendAudioInvite(const SipContext* context, const std::string& sid,
                                    const std::string& tid, const uint16_t port,
                                    const AudioInviteCallback& callback) {
    if (!context->isValid()) {
        _logger.e("eXosip 上下文为空");
        return -1;
    }

    _logger.dBox()
           .add("发送音频 INVITE")
           .addFmt("From (设备): %s", tid.c_str())
           .addFmt("To (平台):   %s", sid.c_str())
           .print();

    _logger.i("构建音频 SDP...");
    if (sid.empty() || tid.empty()) {
        _logger.e("SourceID 或 TargetID 为空");
        return -1;
    }
    const std::string audio_sdp = SdpParser::get()->buildDownstreamSdp(tid,
                                                                       context->getSipParameter().localHost,
                                                                       port,
                                                                       false);
    if (audio_sdp.empty()) {
        _logger.e("构建音频 SDP 失败");
        return -1;
    }

    context->lock();

    // 构建INVITE请求
    osip_message_t* invite = nullptr;
    const int ret = eXosip_call_build_initial_invite(context->getContextPtr(),
                                                     &invite,
                                                     context->getToUri().c_str(),
                                                     context->getFromUri().c_str(),
                                                     context->getProxyUri().c_str(),
                                                     nullptr);
    if (ret < 0 || !invite) {
        _logger.eFmt("构建音频 INVITE 失败: %d", ret);
        context->unlock();
        return -1;
    }
    _logger.i("INVITE 请求已构建");

    /**
     * Subject 格式：SourceID:发送方序列号,TargetID:接收方序列号
     *
     * 示例：34020000002000000001:1,34020000001320000001:1
     * - 34020000002000000001: 平台（音频发送方）
     * - 1: 发送方媒体流序列号
     * - 34020000001320000001: 设备（音频接收方）
     * - 1: 接收方媒体流序列号
     */
    const std::string subject = sid + ":1," + tid + ":1";
    osip_message_set_subject(invite, subject.c_str());
    osip_message_set_body(invite, audio_sdp.c_str(), audio_sdp.length());
    osip_message_set_content_type(invite, "application/sdp");

    // 发送INVITE
    const int call_id = eXosip_call_send_initial_invite(context->getContextPtr(), invite);
    context->unlock();
    if (call_id < 0) {
        _logger.eFmt("发送音频 INVITE 失败: %d", call_id);
        if (callback) {
            callback(2202, StateCode::toString(2202));
        }
        return -1;
    }

    _logger.dBox()
           .add("音频 INVITE 已发送")
           .addFmt("Call ID: %d", call_id)
           .add("等待平台 200 OK 响应...")
           .print();
    return call_id;
}

bool ResponseSender::sendCallErrorResponse(const SipContext* context, const int tid, const int code,
                                           const std::string& reason) {
    if (!context->isValid()) {
        _logger.e("eXosip 上下文为空");
        return false;
    }

    if (tid <= 0) {
        _logger.eFmt("无效的事务ID: %d", tid);
        return false;
    }

    _logger.wFmt("发送呼叫错误响应: %d %s", code, reason.c_str());

    context->lock();

    // 构建错误响应
    osip_message_t* error_response = nullptr;
    eXosip_call_build_answer(context->getContextPtr(), tid, code, &error_response);

    // 设置自定义的原因短语
    if (!reason.empty()) {
        if (error_response->reason_phrase) {
            osip_free(error_response->reason_phrase)
        }
        error_response->reason_phrase = osip_strdup(reason.c_str());
    }

    // 发送响应
    const int result = eXosip_call_send_answer(context->getContextPtr(), tid, code, error_response);
    context->unlock();

    if (result != OSIP_SUCCESS) {
        _logger.eFmt("发送呼叫错误响应失败: %d", result);
        return false;
    }

    _logger.dFmt("呼叫错误响应已发送: %d %s", code, reason.c_str());
    return true;
}