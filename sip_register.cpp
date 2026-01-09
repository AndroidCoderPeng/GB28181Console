//
// Created by pengx on 2025/9/29.
//

#include <netinet/in.h>
#include <arpa/inet.h>
#include <eXosip2/eX_register.h>
#include <osipparser2/osip_message.h>
#include <osipparser2/osip_list.h>
#include <iostream>

#include "sip_register.hpp"
#include "base_config.hpp"
#include "xml_builder.hpp"
#include "sdp_parser.hpp"
#include "pugixml.hpp"
#include "rtp_sender.hpp"

SipRegister::SipRegister(const std::string &local_host, const std::string &server_host, const int server_port,
                         const std::string &server_code, const std::string &server_domain,
                         const std::string &device_code, const std::string &serial_number,
                         const std::string &device_name, const std::string &password,
                         const double longitude, const double latitude)
    : _local_host(local_host),
      _server_host(server_host),
      _server_port(server_port),
      _server_code(server_code),
      _server_domain(server_domain),
      _device_code(device_code),
      _serial_number(serial_number),
      _device_name(device_name),
      _password(password),
      _longitude(longitude),
      _latitude(latitude) {
    eXosip_t *context = eXosip_malloc();
    if (!context) {
        std::cerr << "Failed to allocate eXosip context" << std::endl;
        eXosip_quit(context);
        return;
    }

    int result = eXosip_init(context);
    if (result != 0) {
        std::cerr << "Init eXosip failed: " << result << std::endl;
        eXosip_quit(context);
        return;
    }

    _ex_context_ptr = context;

    // 监听本地端口
    result = eXosip_listen_addr(context, IPPROTO_UDP, nullptr, 5060, AF_INET, 0);
    if (result != 0) {
        std::cerr << "Listen local port failed: " << result << std::endl;
        eXosip_quit(context);
        return;
    }

    // sip事件循环
    if (_sip_event_thread == nullptr) {
        _is_sip_loop_running = true;
        _sip_event_thread = std::make_unique<std::thread>(&SipRegister::sip_event_loop, this);
    }

    std::cout << "SipRegister init success" << std::endl;
}


void SipRegister::sip_event_loop() {
    int event_count = 0;
    auto last_log_time = std::chrono::steady_clock::now();
    while (_is_sip_loop_running) {
        eXosip_event_t *event = eXosip_event_wait(_ex_context_ptr, 100, 0);
        if (event == nullptr) {
            // 定期处理eXosip内部任务
            eXosip_execute(_ex_context_ptr);
            event_count++;
            auto now = std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time);
            if (diff.count() >= 10) {
                std::cout << "Event loop alive, processed " << event_count << " cycles in last 10 seconds" << std::endl;
                event_count = 0;
                last_log_time = now;
            }
            continue;
        }

        const auto response = event->response;
        switch (event->type) {
            case EXOSIP_REGISTRATION_SUCCESS: // 用户注册或者注销成功时触发
                register_success();
                break;
            case EXOSIP_REGISTRATION_FAILURE: // 用户注册或者注销失败时触发
                register_failed(response);
                break;
            case EXOSIP_MESSAGE_NEW: // 新的对话外请求到达时触发，比如：GB28181平台查询设备信息
                platform_event(event);
                break;
            case EXOSIP_CALL_INVITE: // 收到新的呼叫邀请时触发，比如：GB28181平台拉流
                /**
                 * 接收平台发来的SDP协商信息
                 * 解析远程IP和端口
                 * 构造本地SDP响应
                 * 发送200 OK响应建立连接
                 * 开始推流到指定地址
                 * */
                start_push_stream(event);
                break;
            case EXOSIP_CALL_NOANSWER: // 呼叫超时无应答时触发，比如：设备呼叫平台无应答
                std::cout << "No answer for call" << std::endl;
                break;
            case EXOSIP_CALL_CANCELLED: // 呼叫被取消时触发，比如：设备主动取消呼叫平台，被平台挂断就会触发
                std::cout << "Call cancelled" << std::endl;
                break;
            case EXOSIP_CALL_CLOSED: // 收到 BYE 请求，呼叫结束时触发
                /**
                 * 接收BYE请求
                 * 停止媒体推流
                 * 清理会话资源
                 * */
                _sip_event_callback(1001, "停止 H.264+G.711μ 推流");
                break;
            default:
                break;
        }
        eXosip_event_free(event);
    }
}

void SipRegister::sipEventCallback(const sip_event_callback &callback) {
    if (!_ex_context_ptr) {
        std::cerr << "Not initialized eXosip context" << std::endl;
        return;
    }

    _sip_event_callback = callback;

    // 构造 URI
    char client[256];
    snprintf(client, sizeof(client), "sip:%s@%s", _device_code.c_str(), _server_domain.c_str());
    char registrar[256];
    snprintf(registrar, sizeof(registrar), "sip:%s:%d", _server_host.c_str(), _server_port);

    if (_reg_id > 0 && _reg_state != REG_STATE_IDLE) {
        _reg_id = -1;
    }

    osip_message_t *reg = nullptr;
    const int reg_id = eXosip_register_build_initial_register(_ex_context_ptr,
                                                              client,
                                                              registrar,
                                                              nullptr,
                                                              REGISTER_EXPIRED_TIME, &reg);
    if (reg_id < 0) {
        _sip_event_callback(4011, "构建注册请求失败");
        return;
    }
    _reg_id = reg_id;
    _reg_state = REG_STATE_SENT_INITIAL;
    _is_do_unregister = false;

    const int result = eXosip_register_send_register(_ex_context_ptr, _reg_id, reg);
    if (result != 0) {
        _sip_event_callback(4012, "发送注册请求失败");
        _reg_state = REG_STATE_FAILED;
    }
}

void SipRegister::register_success() {
    if (_is_do_unregister) {
        _sip_event_callback(201, "注销成功");
        _reg_state = REG_STATE_IDLE;
        _is_heartbeat_thread_running = false;
        if (_heartbeat_thread_ptr && _heartbeat_thread_ptr->joinable()) {
            _heartbeat_thread_ptr->join();
        }
    } else {
        _sip_event_callback(200, "注册成功");
        _reg_state = REG_STATE_SUCCESS;
        if (_is_heartbeat_thread_running) return;
        _is_heartbeat_thread_running = true;
        _heartbeat_thread_ptr = std::make_unique<std::thread>(&SipRegister::heartbeat, this);
    }
}

void SipRegister::heartbeat() {
    while (_is_heartbeat_thread_running) {
        for (int i = 0; i < HEARTBEAT_INTERVAL * 10 && _is_heartbeat_thread_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!_is_heartbeat_thread_running || !_ex_context_ptr || _reg_id <= 0) {
            break;
        }

        // 构造新的 MESSAGE 请求（发给平台）
        char to_uri[256];
        snprintf(to_uri, sizeof(to_uri), "sip:%s:%d", _server_host.c_str(), _server_port);

        char from_uri[256];
        snprintf(from_uri, sizeof(from_uri), "sip:%s@%s", _device_code.c_str(), _server_domain.c_str());

        osip_message_t *msg = nullptr;
        const int result = eXosip_message_build_request(_ex_context_ptr, &msg, "MESSAGE", to_uri, from_uri, nullptr);
        if (result == 0) {
            osip_message_set_content_type(msg, "Application/MANSCDP+xml");
            const int sn = _sn_cache.fetch_add(1);
            std::string heartbeat_xml = XmlBuilder::buildHeartbeat(std::to_string(sn), _device_code);
            osip_message_set_body(msg, heartbeat_xml.c_str(), heartbeat_xml.length());
            eXosip_message_send_request(_ex_context_ptr, msg);
        }
    }
}

void SipRegister::register_failed(const osip_message_t *response) {
    if (response == nullptr) {
        _sip_event_callback(500, "未知错误");
        _reg_state = REG_STATE_FAILED;
        return;
    }

    if (_reg_state != REG_STATE_IDLE) {
        const int status_code = response->status_code;
        if (status_code == 401 && _reg_state == REG_STATE_SENT_INITIAL) {
            eXosip_add_authentication_info(_ex_context_ptr,
                                           _device_name.c_str(),
                                           _device_code.c_str(),
                                           _password.c_str(),
                                           "MD5", nullptr);
            osip_message_t *auth_reg = nullptr;
            int result = eXosip_register_build_register(_ex_context_ptr, _reg_id,REGISTER_EXPIRED_TIME, &auth_reg);
            if (result != 0) {
                _sip_event_callback(4013, "构建认证注册请求失败");
                _reg_state = REG_STATE_FAILED;
                return;
            }

            result = eXosip_register_send_register(_ex_context_ptr, _reg_id, auth_reg);
            if (result != 0) {
                _sip_event_callback(4014, "发送认证注册请求失败");
                _reg_state = REG_STATE_FAILED;
                return;
            }
            _reg_state = REG_STATE_SENT_AUTH;
        } else {
            _sip_event_callback(status_code, "未知错误");
            _reg_state = REG_STATE_FAILED;
        }
    }
}

void SipRegister::platform_event(const eXosip_event_t *event) {
    if (event->request == nullptr) return;

    // 打印请求详情
    const auto from = event->request->from->url;
    const auto to = event->request->to->url;
    std::cout << "platform event, message from:"
            << (from->username ? from->username : "Unknown") << "@"
            << (from->host ? from->host : "Unknown") << ", to:"
            << (to->username ? to->username : "Unknown") << "@"
            << (to->host ? to->host : "Unknown") << std::endl;

    // 先回复 200 OK
    osip_message_t *ack = nullptr;
    if (eXosip_message_build_answer(_ex_context_ptr, event->tid, 200, &ack) == 0) {
        eXosip_message_send_answer(_ex_context_ptr, event->tid, 200, ack);
    }

    // 解析 Content-Type
    const osip_content_type_t *ct = osip_message_get_content_type(event->request);
    if (!ct || osip_strcasecmp(ct->type, "Application") != 0 ||
        osip_strcasecmp(ct->subtype, "MANSCDP+xml") != 0) {
        std::cerr << "Unsupported content type" << std::endl;
        return;
    }

    // 获取请求体
    const auto *body_ptr = static_cast<osip_body_t *>(osip_list_get(&event->request->bodies, 0));
    if (!body_ptr || !body_ptr->body) return;

    const std::string content(body_ptr->body, body_ptr->length);
    if (content.empty()) {
        std::cerr << "Empty content" << std::endl;
        return;
    }
    pugi::xml_document xml;
    const pugi::xml_parse_result xml_result = xml.load_string(content.c_str());
    if (!xml_result) {
        std::cerr << "Error description: " << xml_result.description() << std::endl;
        return;
    }

    const pugi::xml_node query_node = xml.child("Query");
    if (!query_node) {
        std::cerr << "No Query node found in XML" << std::endl;
        return;
    }

    const std::string cmd_type = query_node.child("CmdType").text().get();
    const std::string sn = query_node.child("SN").text().get();

    std::string response_xml;
    if (cmd_type == "Catalog") {
        response_xml = XmlBuilder::buildCatalog(sn, _device_code, _server_domain, _longitude, _latitude);
    } else if (cmd_type == "DeviceInfo") {
        response_xml = XmlBuilder::buildDeviceInfo(sn, _device_code, _device_name, _serial_number);
    } else if (cmd_type == "ConfigDownload") {
        //<?xml version="1.0" encoding="GB2312"?>
        //<Query>
        // <CmdType>ConfigDownload</CmdType>
        // <SN>10069</SN>
        // <DeviceID>11010800001300021220</DeviceID>
        // <ConfigType>BasicParam</ConfigType>
        //</Query>
        std::cout << "ConfigDownload" << std::endl;
        return;
    } else if (cmd_type == "DeviceStatus") {
        //<?xml version="1.0" encoding="GB2312"?>
        //<Query>
        // <CmdType>DeviceStatus</CmdType>
        // <SN>10072</SN>
        // <DeviceID>11010800001300021220</DeviceID>
        //</Query>
        std::cout << "DeviceStatus" << std::endl;
        return;
    } else {
        std::cout << "Unsupported CmdType: " << cmd_type << std::endl;
        return;
    }

    // 构造新的 MESSAGE 请求（发给平台）
    char to_uri[256];
    snprintf(to_uri, sizeof(to_uri), "sip:%s:%d", _server_host.c_str(), _server_port);

    char from_uri[256];
    snprintf(from_uri, sizeof(from_uri), "sip:%s@%s", _device_code.c_str(), _server_domain.c_str());

    osip_message_t *msg = nullptr;
    const int result = eXosip_message_build_request(_ex_context_ptr, &msg, "MESSAGE", to_uri, from_uri, nullptr);
    if (result == 0) {
        osip_message_set_content_type(msg, "Application/MANSCDP+xml");
        osip_message_set_body(msg, response_xml.c_str(), response_xml.length());
        eXosip_message_send_request(_ex_context_ptr, msg);
    }
}

void SipRegister::start_push_stream(const eXosip_event_t *event) {
    if (event->request == nullptr) return;

    // 打印请求详情
    const auto from = event->request->from->url;
    const auto to = event->request->to->url;
    std::cout << "start push stream, message from:"
            << (from->username ? from->username : "Unknown") << "@"
            << (from->host ? from->host : "Unknown") << ", to:"
            << (to->username ? to->username : "Unknown") << "@"
            << (to->host ? to->host : "Unknown") << std::endl;

    const auto *body_ptr = static_cast<osip_body_t *>(osip_list_get(&event->request->bodies, 0));
    if (!body_ptr || !body_ptr->body) {
        eXosip_call_send_answer(_ex_context_ptr, event->tid, 488, nullptr);
        std::cerr << "Invite body is empty" << std::endl;
        return;
    }
    const std::string sdp(body_ptr->body, body_ptr->length);
    if (sdp.empty()) {
        std::cerr << "Empty sdp" << std::endl;
        return;
    }
    const SdpStruct sdp_struct = SdpParser::parse(sdp);
    if (sdp_struct.remote_port == 0 || sdp_struct.remote_host.empty()) {
        std::cerr << "Failed to parse SDP IP/Port" << std::endl;
        eXosip_call_send_answer(_ex_context_ptr, event->tid, 488, nullptr);
        return;
    }

    // 构建 200 OK 响应
    osip_message_t *answer = nullptr;
    int ret = eXosip_call_build_answer(_ex_context_ptr, event->tid, 200, &answer);
    if (ret != 0 || !answer) {
        std::cerr << "Failed to build 200 OK answer" << std::endl;
        eXosip_call_send_answer(_ex_context_ptr, event->tid, 500, nullptr);
        return;
    }

    // 初始化 RTP Socket
    int local_port;
    if (sdp_struct.transport == "tcp") {
        // 固定返回0
        local_port = RtpSender::get()->initRtpTcpSocket(sdp_struct);
    } else {
        // 返回正整数的port
        local_port = RtpSender::get()->initRtpUdpSocket(sdp_struct);
    }

    if (local_port < 0) {
        std::cerr << "Failed to initialize RTP socket after sending 200 OK" << std::endl;
        return;
    }

    const std::string sdp_answer = SdpParser::buildSdpAnswer(_device_code, _local_host,
                                                             local_port, sdp_struct.ssrc,
                                                             sdp_struct.transport);
    if (sdp_answer.empty()) {
        osip_message_free(answer);
        eXosip_call_send_answer(_ex_context_ptr, event->tid, 500, nullptr);
        return;
    }

    // 设置 SDP body 和 Content-Type
    osip_message_set_body(answer, sdp_answer.c_str(), sdp_answer.length());
    osip_message_set_content_type(answer, "application/sdp");
    ret = eXosip_call_send_answer(_ex_context_ptr, event->tid, 200, answer);
    if (ret != 0) {
        _sip_event_callback(402, "初始化推流失败");
        return;
    }

    _sip_event_callback(1000, "开始 H.264+G.711μ 推流");
}

void SipRegister::unregister() {
    if ((_reg_id <= 0 && _reg_state == REG_STATE_IDLE) || !_ex_context_ptr || !_is_sip_loop_running) {
        return;
    }

    const int current_reg_id = _reg_id;
    _is_do_unregister = true;

    // 构建注销请求(Expires设置为0)
    osip_message_t *un_reg = nullptr;
    int result = eXosip_register_build_register(_ex_context_ptr, current_reg_id, 0, &un_reg);
    if (result != 0) {
        std::cerr << "Build unregister request failed: " << result << std::endl;
        return;
    }

    // 发送注销请求
    result = eXosip_register_send_register(_ex_context_ptr, current_reg_id, un_reg);
    if (result != 0) {
        std::cerr << "Send unregister request failed: " << result << std::endl;
        return;
    }

    _reg_id = -1;
    _reg_state = REG_STATE_IDLE;
}

SipRegister::~SipRegister() {
    _is_sip_loop_running = false;

    if (_sip_event_thread && _sip_event_thread->joinable()) {
        _sip_event_thread->join();
    }

    _is_heartbeat_thread_running = false;
    if (_heartbeat_thread_ptr && _heartbeat_thread_ptr->joinable()) {
        _heartbeat_thread_ptr->join();
    }

    if (_sip_event_callback) {
        _sip_event_callback = nullptr;
    }

    if (_ex_context_ptr) {
        eXosip_quit(_ex_context_ptr);
        _ex_context_ptr = nullptr;
    }

    std::cout << "SipRegister destroyed" << std::endl;
}
