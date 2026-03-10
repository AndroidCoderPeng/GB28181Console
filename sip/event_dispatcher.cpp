//
// Created by pengx on 2026/3/10.
//

#include "event_dispatcher.hpp"

#include <arpa/inet.h>

#include "register_manager.hpp"
#include "response_sender.hpp"
#include "sdp_parser.hpp"
#include "state_code.hpp"
#include "stream_manager.hpp"
#include "xml_builder.hpp"

EventDispatcher::EventDispatcher(SipContext* sip_context, RegisterManager* register_mgr, IEventObserver* event_observer,
                                 IMediaObserver* media_observer) : _logger("EventDispatcher"),
                                                                   _sip_context_ptr(sip_context),
                                                                   _register_mgr_ptr(register_mgr),
                                                                   _event_observer_ptr(event_observer),
                                                                   _media_observer_ptr(media_observer) {
    _logger.i("EventDispatcher created");
}

void EventDispatcher::dispatchEvent(eXosip_event_t* event) {
    if (!event) {
        _logger.e("event is null");
        return;
    }

    try {
        switch (event->type) {
            case EXOSIP_REGISTRATION_SUCCESS:
                handle_registration_success(event);
                break;
            case EXOSIP_REGISTRATION_FAILURE:
                handle_registration_failure(event);
                break;
            case EXOSIP_MESSAGE_NEW:
                handle_message_new(event);
                break;
            case EXOSIP_MESSAGE_ANSWERED:
                handle_message_answered(event);
                break;
            case EXOSIP_MESSAGE_REQUESTFAILURE:
                handle_message_request_failure(event);
                break;
            case EXOSIP_CALL_INVITE:
                handle_call_invite(event);
                break;
            case EXOSIP_CALL_ANSWERED:
                handle_call_answered(event);
                break;
            case EXOSIP_CALL_ACK:
                handle_call_ack(event);
                break;
            case EXOSIP_CALL_CLOSED:
                handle_call_closed(event);
                break;
            case EXOSIP_CALL_RELEASED:
                handle_call_released(event);
                break;
            case EXOSIP_CALL_NOANSWER:
                handle_call_no_answer(event);
                break;
            case EXOSIP_CALL_CANCELLED:
                handle_call_cancelled(event);
                break;
            case EXOSIP_CALL_REQUESTFAILURE:
                handle_call_request_failure(event);
                break;
            case EXOSIP_CALL_SERVERFAILURE:
                handle_call_server_failure(event);
                break;
            case EXOSIP_CALL_GLOBALFAILURE:
                handle_call_global_failure(event);
                break;
            case EXOSIP_SUBSCRIPTION_NOANSWER:
            case EXOSIP_SUBSCRIPTION_ANSWERED:
            case EXOSIP_SUBSCRIPTION_REDIRECTED:
            case EXOSIP_SUBSCRIPTION_REQUESTFAILURE:
            case EXOSIP_SUBSCRIPTION_SERVERFAILURE:
            case EXOSIP_SUBSCRIPTION_GLOBALFAILURE:
            case EXOSIP_SUBSCRIPTION_NOTIFY:
                handle_subscription_events(event);
                break;
            case EXOSIP_IN_SUBSCRIPTION_NEW:
                handle_in_subscription_new();
                break;
            case EXOSIP_NOTIFICATION_NOANSWER:
            case EXOSIP_NOTIFICATION_ANSWERED:
            case EXOSIP_NOTIFICATION_REQUESTFAILURE:
            case EXOSIP_NOTIFICATION_SERVERFAILURE:
            case EXOSIP_NOTIFICATION_GLOBALFAILURE:
                handle_notification_events(event);
                break;
            default:
                handle_unknown_event(event);
                break;
        }
    } catch (const std::exception& e) {
        // 不抛出异常，继续循环，只打印log
        _logger.eBox()
               .addFmt("事件处理异常: %s", e.what())
               .addFmt("事件类型: %d", event->type)
               .print();
    }
}

void EventDispatcher::handle_registration_success(const eXosip_event_t* event) {
    const auto response = event->response;
    const auto status = response ? response->status_code : 0;

    if (_register_mgr_ptr && _register_mgr_ptr->getState() == Sip::RegisterState::SENT_AUTH) {
        _logger.dFmt("注册成功，响应码: %d", status);
        if (_event_observer_ptr) {
            _event_observer_ptr->onLoginSuccess();
        }
    } else {
        _logger.dFmt("注销成功，响应码: %d", status);
        if (_event_observer_ptr) {
            _event_observer_ptr->onLogoutSuccess();
        }
    }
}

void EventDispatcher::handle_registration_failure(const eXosip_event_t* event) {
    const auto response = event->response;
    const auto status = response ? response->status_code : 0;
    _logger.wFmt("注册失败，响应码: %d", status);

    if (response && (status == 401 || status == 407) && _register_mgr_ptr &&
        _register_mgr_ptr->getState() == Sip::RegisterState::SENT_INITIAL) {
        // 添加授权再次注册
        _register_mgr_ptr->handleAuthentication();
    } else {
        if (_event_observer_ptr) {
            _event_observer_ptr->onEventError(status, StateCode::toString(status));
        }
    }
}

void EventDispatcher::handle_message_new(const eXosip_event_t* event) {
    if (!event->request) {
        _logger.e("请求消息为空");
        return;
    }

    auto box = _logger.dBox().add("收到平台消息");
    if (event->request->sip_method) {
        box.addFmt("方法: %s", event->request->sip_method);
    }

    // 打印From头域（谁发送的）
    if (event->request->from && event->request->from->url) {
        char* from_str = nullptr;
        osip_from_to_str(event->request->from, &from_str);
        if (from_str) {
            box.addFmt("来源: %s", from_str);
            osip_free(from_str)
        }
    }
    box.print();

    process_message_event(event);
}

void EventDispatcher::process_message_event(const eXosip_event_t* event) {
    const char* method = event->request->sip_method;
    if (!method) {
        _logger.e("请求方法为空");
        return;
    }

    if (strcmp(method, "MESSAGE") != 0) {
        _logger.wFmt("非 MESSAGE 请求，跳过处理 (方法: %s)，此类请求应由其他事件类型处理", method);
        return;
    }

    _logger.dBox()
           .add("收到平台 MESSAGE 请求")
           .addFmt("Transaction ID: %d", event->tid)
           .print();

    /**
     * GB28181 规定：
     * Content-Type 必须为 "Application/MANSCDP+xml"
     */
    const auto content_type = osip_message_get_content_type(event->request);
    if (!content_type) {
        _logger.w("Content-Type缺失");
        send_event_error_response(event->tid, 415, "Unsupported Media Type");
        return;
    }

    if (osip_strcasecmp(content_type->type, "Application") != 0 ||
        osip_strcasecmp(content_type->subtype, "MANSCDP+xml") != 0) {
        _logger.wFmt("不支持的 Content-Type: %s/%s",
                     content_type->type ? content_type->type : "null",
                     content_type->subtype ? content_type->subtype : "null");
        send_event_error_response(event->tid, 415, "Unsupported Media Type");
        return;
    }

    // 获取消息体
    const osip_body_t* body = nullptr;
    if (osip_list_size(&event->request->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->request->bodies, 0));
    }

    if (!body || !body->body || body->length == 0) {
        _logger.w("消息体为空");
        send_event_error_response(event->tid, 400, "Empty Body");
        return;
    }

    const std::string body_content(body->body, body->length);
    _logger.d("MESSAGE Body");
    _logger.dBox().addBlock(body_content).print();

    // 解析XML
    pugi::xml_document xml;
    const pugi::xml_parse_result parse_result = xml.load_buffer(body->body,
                                                                body->length,
                                                                pugi::parse_default,
                                                                pugi::encoding_auto);
    if (!parse_result) {
        _logger.eFmt("XML 解析失败: %s (offset: %td)", parse_result.description(), parse_result.offset);
        send_event_error_response(event->tid, 400, "Invalid XML");
        return;
    }
    _logger.i("XML 解析成功");

    /**
     * 重要：必须先回复 200 OK，告诉平台消息已收到
     * 然后再处理业务逻辑
     *
     * 原因：
     * 1. 避免平台重发（平台可能有超时重传机制）
     * 2. 业务处理可能耗时较长
     * 3. 符合 SIP 协议规范
     */
    osip_message_t* ack = nullptr;
    eXosip_message_build_answer(_sip_context_ptr->getContextPtr(), event->tid, 200, &ack);
    if (ack) {
        eXosip_message_send_answer(_sip_context_ptr->getContextPtr(), event->tid, 200, ack);
    }

    // 处理消息内容
    const pugi::xml_node root = xml.first_child();
    if (!root) {
        _logger.e("XML 根节点不存在");
        return;
    }

    const std::string root_name = root.name();
    if (root_name == "Query") {
        _logger.i("处理查询类消息");
        process_query_message(xml);
    } else if (root_name == "Notify") {
        _logger.i("处理通知类消息");
        process_notify_message(xml);
    } else {
        _logger.wFmt("未知的消息类型: %s", root_name.c_str());
    }
    _logger.i("平台消息处理完成");
}

void EventDispatcher::process_query_message(const pugi::xml_document& xml) {
    pugi::xml_node query_node = xml.child("Query");
    if (!query_node) {
        _logger.e("XML 中未找到 Query 节点");
        return;
    }

    pugi::xml_node cmd_type_node = query_node.child("CmdType");
    pugi::xml_node sn_node = query_node.child("SN");
    pugi::xml_node device_id_node = query_node.child("DeviceID");
    if (!cmd_type_node || !sn_node) {
        _logger.e("缺少必要字段 (CmdType 或 SN)");
        return;
    }

    std::string cmd_type = cmd_type_node.text().as_string();
    std::string sn = sn_node.text().as_string();
    std::string device_id = device_id_node ? device_id_node.text().as_string() : "";
    if (cmd_type.empty() || sn.empty()) {
        _logger.e("CmdType 或 SN 为空");
        return;
    }

    auto box = _logger.dBox()
                      .add("收到查询请求")
                      .addFmt("CmdType: %s", cmd_type.c_str())
                      .addFmt("SN: %s", sn.c_str());
    if (!device_id.empty()) {
        box.addFmt("DeviceID: %s", device_id.c_str());
    }
    box.print();

    if (!_sip_context_ptr) {
        _logger.e("SIP 上下文为空");
        return;
    }
    std::string response_xml;
    const auto parameter = _sip_context_ptr->getSipParameter();
    if (cmd_type == "Catalog") {
        response_xml = XmlBuilder::get()->buildCatalog(sn,
                                                       parameter.deviceCode,
                                                       parameter.serverDomain,
                                                       parameter.longitude,
                                                       parameter.latitude);
        if (response_xml.empty()) {
            _logger.e("构建 Catalog 响应失败");
            return;
        }
    } else if (cmd_type == "DeviceInfo") {
        response_xml = XmlBuilder::get()->buildDeviceInfo(sn,
                                                          parameter.deviceCode,
                                                          parameter.deviceName,
                                                          parameter.serialNumber);
        if (response_xml.empty()) {
            _logger.e("构建 DeviceInfo 响应失败");
            return;
        }
    } else if (cmd_type == "ConfigDownload") {
        _logger.i("ConfigDownload");
        return;
    } else if (cmd_type == "DeviceStatus") {
        _logger.i("DeviceStatus");
        return;
    } else if (cmd_type == "DeviceControl") {
        _logger.i("DeviceControl");
        return;
    } else {
        _logger.wFmt("不支持的查询类型: %s", cmd_type.c_str());
        return;
    }

    if (!response_xml.empty()) {
        if (ResponseSender::get()->sendEventResponse(_sip_context_ptr, response_xml)) {
            _logger.dBox()
                   .add("查询响应已发送")
                   .addFmt("CmdType: %s", cmd_type.c_str())
                   .addFmt("SN: %s", sn.c_str())
                   .addFmt("响应 XML 长度: %zu 字节", response_xml.length())
                   .print();
        } else {
            _logger.eFmt("发送查询响应失败 (CmdType=%s, SN=%s)", cmd_type.c_str(), sn.c_str());
        }
    }
}

void EventDispatcher::process_notify_message(const pugi::xml_document& xml) {
    const pugi::xml_node notify_node = xml.child("Notify");
    if (!notify_node) {
        _logger.e("XML 中未找到 Notify 节点");
        return;
    }

    const pugi::xml_node cmd_type_node = notify_node.child("CmdType");
    const pugi::xml_node sn_node = notify_node.child("SN");
    if (!cmd_type_node || !sn_node) {
        _logger.e("缺少必要字段 (CmdType 或 SN)");
        return;
    }

    const std::string cmd_type = cmd_type_node.text().as_string();
    const std::string sn = sn_node.text().as_string();
    if (cmd_type.empty() || sn.empty()) {
        _logger.e("CmdType 或 SN 为空");
        return;
    }

    _logger.dBox()
           .add("收到通知消息")
           .addFmt("CmdType: %s", cmd_type.c_str())
           .addFmt("SN: %s", sn.c_str())
           .print();

    if (cmd_type == "Broadcast") {
        /**
         * 平台请求向设备推送语音流
         *
         * 流程：
         * 1. 平台发送 Broadcast 通知
         * 2. 设备向平台发送 INVITE（请求平台推流）
         * 3. 平台回复 200 OK（包含 SDP）
         * 4. 设备接收平台推送的音频流
         * 5. 设备播放音频
         */
        _logger.i("处理语音广播通知");

        const pugi::xml_node source_id_node = notify_node.child("SourceID");
        const pugi::xml_node target_id_node = notify_node.child("TargetID");
        if (!source_id_node || !target_id_node) {
            _logger.e("缺少 SourceID 或 TargetID");
            return;
        }

        const std::string source_id = source_id_node.text().as_string();
        const std::string target_id = target_id_node.text().as_string();
        if (source_id.empty() || target_id.empty()) {
            _logger.e("SourceID 或 TargetID 为空");
            return;
        }

        _logger.i("初始化音频接收器...");
        if (_media_observer_ptr) {
            _media_observer_ptr->initAudioReceiver(source_id, target_id);
        }
    } else {
        _logger.wFmt("不支持的通知类型: %s", cmd_type.c_str());
    }
}

void EventDispatcher::handle_message_answered(const eXosip_event_t* event) {
    if (!event->response) {
        _logger.eFmt("响应事件但 response 为空，type=%d", event->type);
        return;
    }
    _logger.dFmt("消息已被确认，响应码: %d", event->response->status_code);
}

void EventDispatcher::handle_message_request_failure(const eXosip_event_t* event) {
    if (!event->response) {
        _logger.eFmt("响应事件但 response 为空，type=%d", event->type);
        return;
    }
    if (_event_observer_ptr) {
        _event_observer_ptr->onEventError(event->response->status_code,
                                          StateCode::toString(event->response->status_code));
    }
}

void EventDispatcher::handle_call_invite(eXosip_event_t* event) {
    auto box = _logger.dBox()
                      .add("收到呼叫邀请（平台请求推流）")
                      .addFmt("Call ID: %d", event->cid)
                      .addFmt("Dialog ID: %d", event->did);
    if (event->request) {
        // 打印Subject头域（媒体流信息）
        osip_header_t* subject = nullptr;
        osip_message_get_subject(event->request, 0, &subject);
        if (subject && subject->hvalue) {
            box.addFmt("Subject: %s", subject->hvalue);
        }
    }
    box.print();
    if (_media_observer_ptr) {
        _media_observer_ptr->onStartPushStream(event);
    }
}

void EventDispatcher::handle_call_answered(eXosip_event_t* event) {
    const auto response = event->response;
    if (!response) {
        _logger.e("响应事件但 response 为空");
        return;
    }
    _logger.dBox()
           .addFmt("呼叫已接听，响应码: %d", response->status_code)
           .addFmt("Call ID: %d, Dialog ID: %d", event->cid, event->did)
           .print();
    if (_media_observer_ptr) {
        _media_observer_ptr->onStartReceiveAudio(event);
    }
}

void EventDispatcher::handle_call_ack(const eXosip_event_t* event) {
    _logger.dBox()
           .add("收到ACK确认")
           .addFmt("Call ID: %d, Dialog ID: %d", event->cid, event->did)
           .add("媒体会话已建立，可以开始传输流")
           .print();
}

void EventDispatcher::handle_call_closed(const eXosip_event_t* event) {
    _logger.dBox()
           .add("呼叫已结束（收到BYE）")
           .addFmt("Call ID: %d, Dialog ID: %d", event->cid, event->did)
           .print();
    if (_media_observer_ptr) {
        _media_observer_ptr->onMediaClosed(event->cid);
    }
}

void EventDispatcher::handle_call_released(const eXosip_event_t* event) {
    _logger.dBox()
           .add("呼叫资源已释放")
           .addFmt("Call ID: %d", event->cid)
           .print();
}

void EventDispatcher::handle_call_no_answer(const eXosip_event_t* event) {
    _logger.dBox()
           .add("呼叫超时无应答")
           .addFmt("Call ID: %d", event->cid)
           .print();
    if (_event_observer_ptr) {
        _event_observer_ptr->onEventError(1012, StateCode::toString(1012));
    }
}

void EventDispatcher::handle_call_cancelled(const eXosip_event_t* event) {
    _logger.dBox()
           .add("呼叫被取消")
           .addFmt("Call ID: %d", event->cid)
           .print();
    if (_event_observer_ptr) {
        _event_observer_ptr->onEventError(1021, StateCode::toString(1021));
    }
}

void EventDispatcher::handle_call_request_failure(const eXosip_event_t* event) {
    if (!event->response) {
        _logger.e("响应事件但 response 为空");
        return;
    }
    const auto status_code = event->response->status_code;
    if (_event_observer_ptr) {
        _event_observer_ptr->onEventError(status_code, StateCode::toString(status_code));
    }
    _logger.dBox()
           .addFmt("呼叫请求失败，响应码: %d", status_code)
           .addFmt("Call ID: %d", event->cid)
           .print();
}

void EventDispatcher::handle_call_server_failure(const eXosip_event_t* event) {
    if (!event->response) {
        _logger.e("响应事件但 response 为空");
        return;
    }
    _logger.eFmt("服务器错误，响应码: %d", event->response->status_code);
}

void EventDispatcher::handle_call_global_failure(const eXosip_event_t* event) {
    if (!event->response) {
        _logger.e("响应事件但 response 为空");
        return;
    }
    _logger.eFmt("全局失败，响应码: %d", event->response->status_code);
}

void EventDispatcher::handle_subscription_events(const eXosip_event_t* event) {
    _logger.dFmt("订阅事件 (类型: %d)", event->type);
}

void EventDispatcher::handle_in_subscription_new() const {
    _logger.i("[事件] IN_SUBSCRIPTION_NEW - 收到订阅请求");
}

void EventDispatcher::handle_notification_events(const eXosip_event_t* event) {
    _logger.dFmt("通知事件 (类型: %d)", event->type);
}

void EventDispatcher::handle_unknown_event(const eXosip_event_t* event) {
    const auto response = event->response;
    if (!response) {
        _logger.e("响应事件但 response 为空");
        return;
    }
    _logger.dFmt("未知事件类型: %d，响应码: %d", event->type, response->status_code);
}

void EventDispatcher::send_event_error_response(const int tid, const int code, const std::string& reason) const {
    ResponseSender::get()->sendEventErrorResponse(_sip_context_ptr, tid, code, reason);
}
