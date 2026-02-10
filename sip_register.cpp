//
// Created by pengx on 2025/9/29.
//

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <iostream>

#include "eXosip2/eX_register.h"
#include "osipparser2/osip_message.h"
#include "osipparser2/osip_list.h"

#include "sip_register.hpp"
#include "base_config.hpp"
#include "xml_builder.hpp"
#include "rtp_sender.hpp"
#include "utils.hpp"
#include "error_code.hpp"
#include "audio_processor.hpp"

#define SIP_PORT 5060

static void print_invite_info(osip_message_t* request) {
    if (!request) {
        return;
    }

    std::cout << "INVITE 详细信息:" << std::endl;

    // From
    if (request->from && request->from->url) {
        char* from_str = nullptr;
        osip_from_to_str(request->from, &from_str);
        if (from_str) {
            std::cout << "       From: " << from_str << std::endl;
            osip_free(from_str);
        }
    }

    // To
    if (request->to && request->to->url) {
        char* to_str = nullptr;
        osip_to_to_str(request->to, &to_str);
        if (to_str) {
            std::cout << "       To: " << to_str << std::endl;
            osip_free(to_str);
        }
    }

    // Subject（媒体流信息）
    osip_header_t* subject = nullptr;
    osip_message_get_subject(request, 0, &subject);
    if (subject && subject->hvalue) {
        std::cout << "       Subject: " << subject->hvalue << std::endl;
    }

    // Call-ID
    if (request->call_id && request->call_id->number) {
        std::cout << "       Call-ID: " << request->call_id->number << std::endl;
    }

    // CSeq
    if (request->cseq && request->cseq->number) {
        const auto str = request->cseq->method ? request->cseq->method : "";
        std::cout << "       CSeq: %s " << request->cseq->number << str << std::endl;
    }
}

SipRegister::SipRegister(const std::string& local_host, const std::string& server_host, const int server_port,
                         const std::string& server_code, const std::string& server_domain,
                         const std::string& device_code, const std::string& serial_number,
                         const std::string& device_name, const std::string& password,
                         const double longitude, const double latitude) : _local_host(local_host),
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
    // 初始化eXosip
    _ex_context_ptr = eXosip_malloc();
    if (!_ex_context_ptr) {
        std::cerr << "eXosip_malloc failed" << std::endl;
        return;
    }

    int ret = eXosip_init(_ex_context_ptr);
    if (ret != OSIP_SUCCESS) {
        std::cerr << "eXosip_init context failed" << std::endl;
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        return;
    }

    /**
     * 监听本地端口，用于接收Sip信令，与流传输方式并无关联
     * */
    ret = eXosip_listen_addr(_ex_context_ptr,IPPROTO_TCP, nullptr,SIP_PORT,AF_INET, 0);
    if (ret != OSIP_SUCCESS) {
        std::cerr << "eXosip_listen_addr failed" << std::endl;
        eXosip_quit(_ex_context_ptr);
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        return;
    }

    // 设置用户代理。User-Agent 用于标识 SIP 客户端的身份信息，类似于 HTTP 协议中的 User-Agent。
    const std::string user_agent = "GB28181-Device/1.0 " + _device_name;
    eXosip_set_user_agent(_ex_context_ptr, user_agent.c_str());

    // From: sip:设备ID@服务器域
    // To: sip:服务器ID@服务器域
    // Proxy: sip:服务器IP:服务器端口
    _from_uri = "sip:" + _device_code + "@" + _server_domain;
    _to_uri = "sip:" + _server_code + "@" + _server_domain;
    _proxy_uri = "sip:" + _server_host + ":" + std::to_string(_server_port);
    std::cout << "From: " << _from_uri << ", To: " << _to_uri << ", Proxy: " << _proxy_uri << std::endl;

    // sip事件循环
    // 构造函数中不需要判断，因为：
    // 1. _sip_event_thread 在初始化列表中已设为 nullptr
    // 2. 构造函数只会被调用一次
    // 3. 此时对象还未完全构造，不可能有其他线程访问
    try {
        _is_sip_loop_running = true;
        _sip_event_thread_ptr = std::make_unique<std::thread>(&SipRegister::sip_event_loop, this);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create event thread: " << e.what() << std::endl;
        _is_sip_loop_running = false;
        eXosip_quit(_ex_context_ptr);
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        throw;
    }

    std::cout << "SipRegister initialized successfully" << std::endl;
}

void SipRegister::setCallback(const SipEventCallback& event_callback, const PcmDataCallback& pcm_callback,
                              const G711DataCallback& g711_callback) {
    _event_callback = event_callback;
    _pcm_callback = pcm_callback;
    _g711_callback = g711_callback;
}

bool SipRegister::doRegister() {
    return real_sip_registration(REGISTER_EXPIRED_TIME);
}

bool SipRegister::unRegister() {
    std::cout << "请求注销" << std::endl;

    if (_reg_id.load() <= 0) {
        std::cout << "未注册，无需注销" << std::endl;
        _event_callback(400, "未注册");
        return false;
    }

    // 检查状态
    const RegisterState state = _reg_state.load();
    if (state != REG_STATE_SUCCESS && state != REG_STATE_SENT_AUTH) {
        std::cout << "当前状态不允许注销: " << current_register_state().c_str() << std::endl;
        return false;
    }

    // 执行注销（expires=0）
    return real_sip_registration(0);
}

SipRegister::~SipRegister() {
    std::cout << "SipRegister destroying..." << std::endl;

    // ============ 第1步：停止事件循环线程（先停止使用者） ============
    stop_sip_event_loop();

    // ============ 第2步：停止心跳线程 ============
    stop_heartbeat_thread();

    // ============ 第3步：清理 eXosip 上下文（再释放资源） ============
    if (_ex_context_ptr) {
        if (_reg_id > 0) {
            eXosip_lock(_ex_context_ptr);
            eXosip_register_remove(_ex_context_ptr, _reg_id.load());
            eXosip_unlock(_ex_context_ptr);
        }

        eXosip_quit(_ex_context_ptr);
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
    }

    std::cout << "SipRegister destroyed" << std::endl;
}

// ----------------------------- 私有函数 ----------------------------- //
void SipRegister::sip_event_loop() {
    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "|  SIP 事件循环线程已启动" << std::endl;
    std::cout << "|  线程ID: " << std::hash<std::thread::id>{}(std::this_thread::get_id()) << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;

    // 用于统计空闲循环次数
    int idle_cycle_count = 0;
    // 记录上次打印日志的时间点
    auto last_heartbeat_log_time = std::chrono::steady_clock::now();

    // 主循环：持续运行直到收到停止信号
    while (_is_sip_loop_running.load()) {
        if (!_ex_context_ptr) {
            std::cerr << "eXosip context is null, exiting event loop" << std::endl;
            break;
        }

        // 等待Sip事件（阻塞100毫秒）
        eXosip_event_t* event = eXosip_event_wait(_ex_context_ptr, 100, 0);
        if (event == nullptr) {
            if (_ex_context_ptr) {
                // 执行 eXosip 内部定时任务——处理重传、超时等内部维护工作
                eXosip_execute(_ex_context_ptr);
            }
            idle_cycle_count++;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat_log_time);
            if (elapsed.count() >= 30) {
                std::cout << "事件循环心跳 - 空闲循环次数: " << idle_cycle_count << std::endl;
                std::cout << "当前注册状态: " << current_register_state().c_str() << std::endl;

                // 重置计数器和时间戳
                idle_cycle_count = 0;
                last_heartbeat_log_time = now;
            }
            continue;
        }

        // ============ 收到事件，重置空闲计数并获取响应消息 ============
        idle_cycle_count = 0;
        dispatch_sip_event(event);

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
    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "│  SIP 事件循环线程正常退出" << std::endl;
    std::cout << "│  总空闲循环次数: " << idle_cycle_count << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;
}

void SipRegister::stop_sip_event_loop() {
    std::lock_guard<std::mutex> lock(_event_loop_mutex);

    if (!_is_sip_loop_running.load()) {
        return;
    }

    std::cout << "正在停止 SIP 事件循环..." << std::endl;

    _is_sip_loop_running = false;

    if (_sip_event_thread_ptr && _sip_event_thread_ptr->joinable()) {
        _sip_event_thread_ptr->join();
    }

    _sip_event_thread_ptr.reset();

    std::cout << "SIP 事件循环已停止" << std::endl;
}

void SipRegister::dispatch_sip_event(const eXosip_event_t* event) {
    const auto response = event->response;
    try {
        switch (event->type) {
            case EXOSIP_REGISTRATION_SUCCESS:
            {
                /**
                 * 注册成功事件
                 * 触发时机：
                 * 1. 首次注册成功（收到200 OK）
                 * 2. 注销成功（发送expires=0后收到200 OK）
                 * 3. 刷新注册成功
                 */
                std::cout << "[事件] REGISTRATION_SUCCESS - 注册/注销成功" << std::endl;
                if (response) {
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cout << "      响应码: " << response->status_code << reason << std::endl;
                }
                register_success();
                break;
            }

            case EXOSIP_REGISTRATION_FAILURE:
            {
                /**
                 * 注册失败事件
                 * 触发时机：
                 * 1. 认证失败（401/407需要认证）
                 * 2. 服务器拒绝（403 Forbidden）
                 * 3. 其他错误（4xx/5xx）
                 */
                std::cout << "[事件] REGISTRATION_FAILURE - 注册/注销失败" << std::endl;
                if (response) {
                    const auto status_code = response->status_code;
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cout << "      响应码: " << response->status_code << reason << std::endl;
                    if ((status_code == 401 || status_code == 407) &&
                        _reg_state.load() == REG_STATE_SENT_INITIAL) {
                        // 添加授权再次注册
                        sip_registration_with_auth();
                    } else {
                        _event_callback(status_code, ErrorCode::toString(status_code).data());
                        _reg_state = REG_STATE_FAILED;
                    }
                }
                break;
            }

            case EXOSIP_MESSAGE_NEW:
            {
                /**
                 * 收到新的MESSAGE请求
                 * 触发时机：
                 * 1. 平台查询设备信息（DeviceInfo）
                 * 2. 平台查询设备目录（Catalog）
                 * 3. 平台查询设备状态（DeviceStatus）
                 * 4. 平台发送控制命令（PTZ、录像等）
                 * 5. 平台发送通知（语音广播等）
                 */
                std::cout << "[事件] MESSAGE_NEW - 收到平台消息" << std::endl;
                if (event->request) {
                    // 打印请求方法
                    std::cout << "      方法: " << event->request->sip_method << std::endl;

                    // 打印From头域（谁发送的）
                    if (event->request->from && event->request->from->url) {
                        char* from_str = nullptr;
                        osip_from_to_str(event->request->from, &from_str);
                        if (from_str) {
                            std::cout << "      来源: " << from_str << std::endl;
                            osip_free(from_str)
                        }
                    }
                }
                platform_event(event);
                break;
            }

            case EXOSIP_MESSAGE_ANSWERED:
            {
                /**
                 * MESSAGE请求收到响应
                 * 触发时机：发送MESSAGE后收到平台的200 OK
                 */
                std::cout << "[事件] MESSAGE_ANSWERED - 消息已被确认" << std::endl;
                if (response) {
                    std::cout << "      响应码: " << response->status_code << std::endl;
                }
                break;
            }

            case EXOSIP_MESSAGE_REQUESTFAILURE:
            {
                /**
                 * MESSAGE请求失败
                 * 触发时机：发送MESSAGE后收到4xx/5xx错误
                 */
                std::cerr << "[事件] MESSAGE_REQUESTFAILURE - 消息请求失败" << std::endl;
                if (response) {
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cerr << "      响应码: " << response->status_code << reason << std::endl;
                }
                break;
            }

            case EXOSIP_CALL_INVITE:
            {
                /**
                 * 收到INVITE请求（平台发起呼叫）
                 * 触发时机：
                 * 1. 平台请求实时视频点播
                 * 2. 平台请求历史视频回放
                 * 3. 平台请求视频下载
                 *
                 * 处理流程：
                 * 1. 解析SDP，获取平台RTP接收地址
                 * 2. 初始化本地RTP发送器
                 * 3. 回复200 OK（包含本地SDP）
                 * 4. 开始推送音视频流
                 */
                std::cout << "[事件] CALL_INVITE - 收到呼叫邀请（平台请求推流）" << std::endl;
                std::cout << "      Call ID: " << event->cid << " Dialog ID: " << event->did << std::endl;

                if (event->request) {
                    // 打印Subject头域（媒体流信息）
                    osip_header_t* subject = nullptr;
                    osip_message_get_subject(event->request, 0, &subject);
                    if (subject && subject->hvalue) {
                        std::cout << "      Subject: " << subject->hvalue << std::endl;
                    }
                }

                start_push_stream(event);
                break;
            }

            case EXOSIP_CALL_ANSWERED:
            {
                /**
                 * INVITE请求收到200 OK响应（设备发起的呼叫被接受）
                 * 触发时机：
                 * 1. 设备发起语音对讲INVITE后，平台回复200 OK
                 *
                 * 处理流程：
                 * 1. 解析平台返回的SDP
                 * 2. 初始化本地RTP接收器
                 * 3. 发送ACK确认
                 * 4. 开始接收音频流
                 */
                std::cout << "[事件] CALL_ANSWERED - 呼叫已接听" << std::endl;
                std::cout << "      Call ID: " << event->cid << " Dialog ID: " << event->did << std::endl;
                if (response) {
                    std::cout << "      响应码: " << response->status_code << std::endl;
                }

                start_receive_audio(event);
                break;
            }

            case EXOSIP_CALL_ACK:
            {
                /**
                 * 收到ACK确认
                 * 触发时机：发送200 OK后收到平台的ACK
                 * 说明：三次握手完成，媒体流可以正式传输了
                 */
                std::cout << "[事件] CALL_ACK - 收到ACK确认" << std::endl;
                std::cout << "      Call ID: " << event->cid << " Dialog ID: " << event->did << std::endl;
                std::cout << "媒体会话已建立，可以开始传输流" << std::endl;
                break;
            }

            case EXOSIP_CALL_CLOSED:
            {
                /**
                 * 呼叫结束（收到BYE请求）
                 * 触发时机：
                 * 1. 平台主动停止点播（发送BYE）
                 * 2. 平台停止语音对讲
                 *
                 * 处理流程：
                 * 1. 停止RTP推流/接收
                 * 2. 释放媒体资源
                 * 3. 回复200 OK
                 */
                std::cout << "[事件] CALL_CLOSED - 呼叫已结束（收到BYE）" << std::endl;
                std::cout << "      Call ID: " << event->cid << " Dialog ID: " << event->did << std::endl;

                // 检查是音频呼叫还是视频呼叫
                if (event->cid == _audio_call_id.load()) {
                    std::cout << "音频呼叫结束" << std::endl;
                    stop_receive_audio();
                } else if (event->cid == _video_call_id.load()) {
                    std::cout << "视频呼叫结束" << std::endl;
                    stop_push_stream();
                }
                break;
            }

            case EXOSIP_CALL_RELEASED:
            {
                /**
                 * 呼叫资源已释放
                 * 触发时机：BYE流程完成后
                 */
                std::cout << "[事件] CALL_RELEASED - 呼叫资源已释放" << std::endl;
                std::cout << "      Call ID: " << event->cid << std::endl;
                break;
            }

            case EXOSIP_CALL_NOANSWER:
            {
                /**
                 * 呼叫无应答超时
                 * 触发时机：发送INVITE后长时间未收到响应
                 */
                std::cout << "[事件] CALL_NOANSWER - 呼叫超时无应答" << std::endl;
                std::cout << "      Call ID: " << event->cid << std::endl;
                _event_callback(408, "呼叫超时无应答");
                break;
            }

            case EXOSIP_CALL_CANCELLED:
            {
                /**
                 * 呼叫被取消
                 * 触发时机：
                 * 1. 发送INVITE后，对方发送CANCEL
                 * 2. 自己发送CANCEL取消呼叫
                 */
                std::cout << "[事件] CALL_CANCELLED - 呼叫被取消" << std::endl;
                std::cout << "      Call ID: " << event->cid << std::endl;
                _event_callback(487, "呼叫被取消");
                break;
            }

            case EXOSIP_CALL_REQUESTFAILURE:
            {
                /**
                 * 呼叫请求失败
                 * 触发时机：INVITE收到4xx/5xx错误响应
                 */
                std::cerr << "[事件] CALL_REQUESTFAILURE - 呼叫请求失败" << std::endl;
                if (response) {
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cerr << "      响应码: " << response->status_code << reason << std::endl;
                    const std::string error = "呼叫失败: " + ErrorCode::toString(response->status_code);
                    _event_callback(response->status_code, error.data());
                }
                std::cerr << "      Call ID: " << event->cid << std::endl;
                break;
            }

            case EXOSIP_CALL_SERVERFAILURE:
            {
                /**
                 * 服务器错误
                 * 触发时机：收到5xx服务器错误响应
                 */
                std::cerr << "[事件] CALL_SERVERFAILURE - 服务器错误" << std::endl;
                if (response) {
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cerr << "      响应码: " << response->status_code << reason << std::endl;
                }
                break;
            }

            case EXOSIP_CALL_GLOBALFAILURE:
            {
                /**
                 * 全局失败
                 * 触发时机：收到6xx全局失败响应
                 */
                std::cerr << "[事件] CALL_GLOBALFAILURE - 全局失败" << std::endl;
                if (response) {
                    const auto reason = response->reason_phrase ? response->reason_phrase : "";
                    std::cerr << "      响应码: " << response->status_code << reason << std::endl;
                }
                break;
            }

            case EXOSIP_SUBSCRIPTION_NOANSWER:
            case EXOSIP_SUBSCRIPTION_ANSWERED:
            case EXOSIP_SUBSCRIPTION_REDIRECTED:
            case EXOSIP_SUBSCRIPTION_REQUESTFAILURE:
            case EXOSIP_SUBSCRIPTION_SERVERFAILURE:
            case EXOSIP_SUBSCRIPTION_GLOBALFAILURE:
            case EXOSIP_SUBSCRIPTION_NOTIFY:
            {
                /**
                 * 订阅相关事件（GB28181较少使用）
                 */
                std::cout << "[事件] SUBSCRIPTION - 订阅事件 (类型: " << event->type << ")" << std::endl;
                break;
            }
            case EXOSIP_IN_SUBSCRIPTION_NEW:
            {
                /**
                 * 收到新的订阅请求
                 */
                std::cout << "[事件] IN_SUBSCRIPTION_NEW - 收到订阅请求" << std::endl;
                break;
            }
            case EXOSIP_NOTIFICATION_NOANSWER:
            case EXOSIP_NOTIFICATION_ANSWERED:
            case EXOSIP_NOTIFICATION_REQUESTFAILURE:
            case EXOSIP_NOTIFICATION_SERVERFAILURE:
            case EXOSIP_NOTIFICATION_GLOBALFAILURE:
            {
                /**
                 * 通知相关事件
                 */
                std::cout << "[事件] NOTIFICATION - 通知事件 (类型: " << event->type << ")" << std::endl;
                break;
            }
            default:
            {
                /**
                 * 未处理的事件类型
                 */
                std::cout << "[事件] UNKNOWN - 未知事件类型: " << event->type << std::endl;
                if (response) {
                    std::cout << "      响应码: " << response->status_code << std::endl;
                }
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "事件处理异常: " << e.what() << std::endl;
        std::cerr << "事件类型: " << event->type << std::endl;
        // 不抛出异常，继续循环
    } catch (...) {
        std::cerr << "事件处理发生未知异常" << std::endl;
        std::cerr << "事件类型: " << event->type << std::endl;
    }
}

bool SipRegister::real_sip_registration(const uint16_t expires) {
    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        _event_callback(5001, "SIP协议栈未初始化");
        _reg_state = REG_STATE_FAILED;
        return false;
    }

    std::lock_guard<std::mutex> lock(_register_mutex);

    const bool is_unregister = (expires == 0);
    if (is_unregister) {
        std::cout << "┌─────────────────────────────────────┐" << std::endl;
        std::cout << "│  开始执行注销                         │" << std::endl;
        std::cout << "└─────────────────────────────────────┘" << std::endl;

        // 检查是否已注册
        if (_reg_id.load() <= 0) {
            std::cout << "未注册，无需注销 (reg_id=" << _reg_id.load() << ")" << std::endl;
            _event_callback(400, "未注册，无需注销");
            return false;
        }

        std::cout << "当前 reg_id: " << _reg_id.load() << std::endl;
        _is_do_unregister = true;
    } else {
        std::cout << "┌─────────────────────────────────────┐" << std::endl;
        std::cout << "│  开始执行注册" << std::endl;
        std::cout << "│  Expires: " << expires << "秒" << std::endl;
        std::cout << "└─────────────────────────────────────┘" << std::endl;

        _is_do_unregister = false;
    }

    eXosip_lock(_ex_context_ptr);

    osip_message_t* reg_msg = nullptr;
    int result = OSIP_SUCCESS;

    if (!is_unregister) {
        const int reg_id = eXosip_register_build_initial_register(_ex_context_ptr,
                                                                  _from_uri.c_str(),
                                                                  _proxy_uri.c_str(),
                                                                  nullptr,
                                                                  expires,
                                                                  &reg_msg);
        if (reg_id < 0 || !reg_msg) {
            std::cerr << "构建注册消息失败: " << reg_id << std::endl;
            eXosip_unlock(_ex_context_ptr);
            _event_callback(4011, "构建注册请求失败");
            return false;
        }

        // 保存 reg_id
        _reg_id = reg_id;
        _reg_state = REG_STATE_SENT_INITIAL;

        std::cout << "注册消息已构建，reg_id: " << reg_id << std::endl;
    } else {
        const int reg_id = _reg_id.load();

        // 构建注销消息（使用现有的 reg_id）
        result = eXosip_register_build_register(_ex_context_ptr, reg_id, 0, &reg_msg);
        if (result != OSIP_SUCCESS || !reg_msg) {
            std::cerr << "构建注销消息失败: " << result << std::endl;
            eXosip_unlock(_ex_context_ptr);
            _event_callback(4021, "构建注销请求失败");
            return false;
        }

        std::cout << "注销消息已构建" << std::endl;
    }

    const int reg_id = _reg_id.load();
    result = eXosip_register_send_register(_ex_context_ptr, reg_id, reg_msg);
    eXosip_unlock(_ex_context_ptr);

    if (result != OSIP_SUCCESS) {
        const auto str = is_unregister ? "注销" : "注册";
        std::cout << "发送" << str << "消息失败: " << result << std::endl;
        if (is_unregister) {
            _is_do_unregister = false;
            _event_callback(4022, "发送注销请求失败");
        } else {
            _reg_id = -1;
            _reg_state = REG_STATE_FAILED;
            _event_callback(4012, "发送注册请求失败");
        }
        return false;
    }
    return true;
}

bool SipRegister::sip_registration_with_auth() {
    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "│  开始认证注册流程" << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;

    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        _event_callback(5001, "SIP协议栈未初始化");
        _reg_state = REG_STATE_FAILED;
        return false;
    }

    const int reg_id = _reg_id.load();
    if (reg_id <= 0) {
        std::cerr << "无效的 reg_id: " << reg_id << std::endl;
        _event_callback(4015, "无效的注册ID");
        _reg_state = REG_STATE_FAILED;
        return false;
    }

    eXosip_lock(_ex_context_ptr);

    int result = eXosip_add_authentication_info(_ex_context_ptr,
                                                _device_name.c_str(),
                                                _device_code.c_str(),
                                                _password.c_str(),
                                                "MD5",
                                                nullptr);
    if (result != OSIP_SUCCESS) {
        std::cerr << "添加认证信息失败: " << result << std::endl;
        eXosip_unlock(_ex_context_ptr);
        _event_callback(4013, "添加认证信息失败");
        _reg_state = REG_STATE_FAILED;
        return false;
    }

    std::cout << "认证信息已添加" << std::endl;

    osip_message_t* auth_reg = nullptr;
    result = eXosip_register_build_register(_ex_context_ptr, reg_id,REGISTER_EXPIRED_TIME, &auth_reg);
    if (result != OSIP_SUCCESS || !auth_reg) {
        std::cerr << "构建认证注册消息失败: " << result << std::endl;
        eXosip_unlock(_ex_context_ptr);
        _event_callback(4013, "构建认证注册请求失败");
        _reg_state = REG_STATE_FAILED;
        return false;
    }

    std::cout << "认证注册消息已构建" << std::endl;

    result = eXosip_register_send_register(_ex_context_ptr, _reg_id, auth_reg);
    eXosip_unlock(_ex_context_ptr);
    if (result != OSIP_SUCCESS) {
        std::cerr << "发送认证注册请求失败: " << result << std::endl;
        _event_callback(4014, "发送认证注册请求失败");
        _reg_state = REG_STATE_FAILED;
        return false;
    }
    _reg_state.store(REG_STATE_SENT_AUTH);
    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "│  认证注册请求已发送" << std::endl;
    std::cout << "│  等待平台响应..." << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;
    return true;
}

void SipRegister::register_success() {
    if (_is_do_unregister.load()) {
        std::cout << "注销成功" << std::endl;
        _event_callback(201, "注销成功");
        _reg_state = REG_STATE_IDLE;
        _reg_id = -1;

        stop_heartbeat_thread();
    } else {
        std::cout << "注册成功" << std::endl;
        _event_callback(200, "注册成功");
        _reg_state = REG_STATE_SUCCESS;

        // ============ 启动心跳线程 ============
        if (_is_heartbeat_thread_running.load()) {
            std::cout << "停止旧的心跳线程" << std::endl;
            stop_heartbeat_thread();
        }

        if (!start_heartbeat_thread()) {
            std::cout << "启动心跳线程失败" << std::endl;
        }
    }
}

bool SipRegister::start_heartbeat_thread() {
    std::lock_guard<std::mutex> lock(_heartbeat_mutex);

    // 防止重复启动
    if (_is_heartbeat_thread_running.load()) {
        std::cout << "心跳线程已在运行" << std::endl;
        return false;
    }

    if (_heartbeat_thread_ptr && _heartbeat_thread_ptr->joinable()) {
        std::cout << "心跳线程对象已存在" << std::endl;
        return false;
    }

    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空，无法启动心跳" << std::endl;
        return false;
    }

    std::cout << "启动心跳线程" << std::endl;

    try {
        _is_heartbeat_thread_running = true;
        _heartbeat_thread_ptr = std::make_unique<std::thread>(&SipRegister::heartbeat, this);
        std::cout << "心跳线程启动成功" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "启动心跳线程失败: " << e.what() << std::endl;
        _is_heartbeat_thread_running = false;
        return false;
    }
}

void SipRegister::heartbeat() {
    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "│  心跳线程已启动" << std::endl;
    std::cout << "│  心跳间隔: " << HEARTBEAT_INTERVAL << "秒" << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;

    int heartbeat_count = 0;
    while (_is_heartbeat_thread_running.load()) {
        // ============ 分段睡眠，便于快速响应停止信号 ============
        /**
         * 为什么要分段睡眠？
         *
         * 如果使用：
         *   std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_INTERVAL));
         * 那么从设置停止标志到线程实际退出，最长需要等待 HEARTBEAT_INTERVAL 秒（60秒）
         *
         * 使用分段睡眠：
         *   for (int i = 0; i < HEARTBEAT_INTERVAL * 10; i++) {
         *       sleep(100ms);  // 每次只睡100ms
         *       if (停止标志) break;  // 及时检查停止标志
         *   }
         * 这样最长只需要等待 100ms 就能响应停止信号
         */
        for (int i = 0; i < HEARTBEAT_INTERVAL * 10 && _is_heartbeat_thread_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // ============ 睡眠结束后再次检查停止标志 ============
        /**
         * 为什么要再次检查？
         * - 可能在最后一次100ms睡眠后，停止标志被设置
         * - 避免在停止状态下还发送心跳
         */
        if (!_is_heartbeat_thread_running.load()) {
            std::cout << "检测到停止信号，退出心跳循环" << std::endl;
            break;
        }

        if (!_ex_context_ptr) {
            std::cerr << "eXosip 上下文为空，心跳线程退出" << std::endl;
            break;
        }

        if (_reg_id.load() <= 0) {
            std::cout << "未注册 (reg_id=" << _reg_id.load() << ")，跳过本次心跳" << std::endl;
            continue;
        }

        // ============ 构建并发送心跳消息 ============
        heartbeat_count++;
        std::cout << "发送心跳 #" << heartbeat_count << ": From=" << _from_uri << ", To=" << _to_uri << std::endl;

        eXosip_lock(_ex_context_ptr);

        osip_message_t* msg = nullptr;
        const int ret = eXosip_message_build_request(_ex_context_ptr,
                                                     &msg,
                                                     "MESSAGE",
                                                     _to_uri.c_str(),
                                                     _from_uri.c_str(),
                                                     _proxy_uri.c_str());
        if (ret != OSIP_SUCCESS || !msg) {
            std::cerr << "构建心跳消息失败: " << ret << std::endl;
            eXosip_unlock(_ex_context_ptr);
            continue;
        }

        osip_message_set_content_type(msg, "Application/MANSCDP+xml");

        const int sn = _sn_counter.fetch_add(1);
        std::string heartbeat_xml = XmlBuilder::buildHeartbeat(std::to_string(sn), _device_code);

        osip_message_set_body(msg, heartbeat_xml.c_str(), heartbeat_xml.length());

        // 发送心跳
        eXosip_message_send_request(_ex_context_ptr, msg);
        eXosip_unlock(_ex_context_ptr);
    }

    std::cout << "┌─────────────────────────────────────┐" << std::endl;
    std::cout << "│  心跳线程正常退出" << std::endl;
    std::cout << "│  共发送心跳: " << heartbeat_count << " 次" << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;
}

void SipRegister::stop_heartbeat_thread() {
    std::lock_guard<std::mutex> lock(_heartbeat_mutex);

    if (!_is_heartbeat_thread_running.load()) {
        return;
    }

    std::cout << "正在停止心跳线程..." << std::endl;

    _is_heartbeat_thread_running = false;

    if (_heartbeat_thread_ptr && _heartbeat_thread_ptr->joinable()) {
        _heartbeat_thread_ptr->join();
    }

    _heartbeat_thread_ptr.reset();

    std::cout << "心跳线程已停止" << std::endl;
}

void SipRegister::platform_event(const eXosip_event_t* event) {
    if (!event) {
        std::cerr << "事件对象为空" << std::endl;
        return;
    }

    if (!event->request) {
        std::cerr << "请求消息为空" << std::endl;
        return;
    }

    /**
     * EXOSIP_MESSAGE_NEW 会捕获所有请求类型：
     * - MESSAGE: GB28181 查询/通知/控制消息（需要处理）
     * - BYE: 结束呼叫（不需要处理，由 EXOSIP_CALL_CLOSED 处理）
     * - ACK: 应答确认（不需要处理）
     * - OPTIONS: 选项查询（不需要处理）
     * - INFO: 信息请求（不需要处理）
     */
    const char* method = event->request->sip_method;
    if (!method) {
        std::cerr << "请求方法为空" << std::endl;
        return;
    }

    std::cout << "[事件] MESSAGE_NEW - 收到请求" << std::endl;
    std::cout << "      方法: " << method << std::endl;

    if (event->request->from && event->request->from->url) {
        char* from_str = nullptr;
        osip_from_to_str(event->request->from, &from_str);
        if (from_str) {
            std::cout << "      来源: " << from_str << std::endl;
            osip_free(from_str)
        }
    }

    if (strcmp(method, "MESSAGE") != 0) {
        std::cout << "非 MESSAGE 请求，跳过处理 (方法: " << method << ")，此类请求应由其他事件类型处理" << std::endl;
        return;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  收到平台 MESSAGE 请求" << std::endl;
    std::cout << "│  Transaction ID: " << event->tid << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    /**
     * GB28181 规定：
     * Content-Type 必须为 "Application/MANSCDP+xml"
     */
    const osip_content_type_t* content_type = osip_message_get_content_type(event->request);
    if (!content_type) {
        std::cerr << "Content-Type缺失" << std::endl;
        send_error_response(event->tid, 415, "Unsupported Media Type");
        return;
    }

    if (osip_strcasecmp(content_type->type, "Application") != 0 ||
        osip_strcasecmp(content_type->subtype, "MANSCDP+xml") != 0) {
        const auto type = content_type->type ? content_type->type : "null";
        const auto subtype = content_type->subtype ? content_type->subtype : "null";
        std::cerr << "不支持的 Content-Type: " << type << "/" << subtype << std::endl;
        send_error_response(event->tid, 415, "Unsupported Media Type");
        return;
    }

    const osip_body_t* body = nullptr;
    if (osip_list_size(&event->request->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->request->bodies, 0));
    }

    if (!body || !body->body || body->length == 0) {
        std::cerr << "消息体为空" << std::endl;
        send_error_response(event->tid, 400, "Empty Body");
        return;
    }

    const std::string body_content(body->body, body->length);
    std::cout << "┌──────────── MESSAGE Body ────────────┐" << std::endl;
    std::cout << body_content.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    pugi::xml_document xml;
    const pugi::xml_parse_result parse_result = xml.load_buffer(body->body,
                                                                body->length,
                                                                pugi::parse_default,
                                                                pugi::encoding_auto);
    if (!parse_result) {
        std::cerr << "XML 解析失败: " << parse_result.description() << " (offset: " << parse_result.offset << ")" <<
                std::endl;
        send_error_response(event->tid, 400, "Invalid XML");
        return;
    }
    std::cout << "XML 解析成功" << std::endl;

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
    if (eXosip_message_build_answer(_ex_context_ptr, event->tid, 200, &ack) == OSIP_SUCCESS &&
        ack != nullptr) {
        eXosip_message_send_answer(_ex_context_ptr, event->tid, 200, ack);
    }

    const pugi::xml_node root = xml.first_child();
    if (!root) {
        std::cerr << "XML 根节点不存在" << std::endl;
        return;
    }

    const std::string root_name = root.name();
    if (root_name == "Query") {
        std::cout << "处理查询类消息" << std::endl;
        response_query_sip(xml);
    } else if (root_name == "Notify") {
        std::cout << "处理通知类消息" << std::endl;
        response_notify_sip(xml);
    } else {
        std::cerr << "未知的消息类型: " << root_name.c_str() << std::endl;
    }
    std::cout << "平台消息处理完成" << std::endl;
}

void SipRegister::response_query_sip(const pugi::xml_document& xml) {
    const pugi::xml_node query_node = xml.child("Query");
    if (!query_node) {
        std::cerr << "XML 中未找到 Query 节点" << std::endl;
        return;
    }

    const pugi::xml_node cmd_type_node = query_node.child("CmdType");
    const pugi::xml_node sn_node = query_node.child("SN");
    const pugi::xml_node device_id_node = query_node.child("DeviceID");
    if (!cmd_type_node || !sn_node) {
        std::cerr << "缺少必要字段 (CmdType 或 SN)" << std::endl;
        return;
    }

    const std::string cmd_type = cmd_type_node.text().as_string();
    const std::string sn = sn_node.text().as_string();
    const std::string device_id = device_id_node ? device_id_node.text().as_string() : "";
    if (cmd_type.empty() || sn.empty()) {
        std::cerr << "CmdType 或 SN 为空" << std::endl;
        return;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  收到查询请求" << std::endl;
    std::cout << "│  CmdType: " << cmd_type.c_str() << std::endl;
    std::cout << "│  SN: " << sn.c_str() << std::endl;
    if (!device_id.empty()) {
        std::cout << "│  DeviceID: " << device_id.c_str() << std::endl;
    }
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    std::string response_xml;
    if (cmd_type == "Catalog") {
        response_xml = XmlBuilder::buildCatalog(sn, _device_code, _server_domain, _longitude, _latitude);
        if (response_xml.empty()) {
            std::cerr << "构建 Catalog 响应失败" << std::endl;
            return;
        }
    } else if (cmd_type == "DeviceInfo") {
        response_xml = XmlBuilder::buildDeviceInfo(sn, _device_code, _device_name, _serial_number);
        if (response_xml.empty()) {
            std::cerr << "构建 DeviceInfo 响应失败" << std::endl;
            return;
        }
    } else if (cmd_type == "ConfigDownload") {
        std::cout << "ConfigDownload" << std::endl;
        return;
    } else if (cmd_type == "DeviceStatus") {
        std::cout << "DeviceStatus" << std::endl;
        return;
    } else if (cmd_type == "DeviceControl") {
        std::cout << "DeviceControl" << std::endl;
        return;
    } else {
        std::cerr << "不支持的查询类型: " << cmd_type.c_str() << std::endl;
        return;
    }

    if (!response_xml.empty()) {
        std::cout << "响应 XML 长度: " << response_xml.length() << " 字节" << std::endl;
        if (send_message_request(response_xml)) {
            std::cout << "┌──────────────────────────────────────┐" << std::endl;
            std::cout << "│  查询响应已发送" << std::endl;
            std::cout << "│  CmdType: " << cmd_type.c_str() << std::endl;
            std::cout << "│  SN: " << sn.c_str() << std::endl;
            std::cout << "└──────────────────────────────────────┘" << std::endl;
        } else {
            std::cerr << "发送查询响应失败 (CmdType=" << cmd_type.c_str() << ", SN=" << sn.c_str() << ")" << std::endl;
        }
    }
}

bool SipRegister::send_message_request(const std::string& response) const {
    if (response.empty()) {
        std::cerr << "消息体为空" << std::endl;
        return false;
    }

    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        return false;
    }

    eXosip_lock(_ex_context_ptr);

    osip_message_t* msg = nullptr;
    int result = eXosip_message_build_request(_ex_context_ptr,
                                              &msg,
                                              "MESSAGE",
                                              _to_uri.c_str(),
                                              _from_uri.c_str(),
                                              _proxy_uri.c_str());
    if (result != OSIP_SUCCESS || !msg) {
        std::cerr << "构建 MESSAGE 请求失败: " << std::endl;
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    std::cout << "MESSAGE 请求已构建" << std::endl;

    /**
     * GB28181 规定：
     * Content-Type 必须为 "Application/MANSCDP+xml"
     * 注意大小写：Application (首字母大写)
     */
    result = osip_message_set_content_type(msg, "Application/MANSCDP+xml");
    if (result != OSIP_SUCCESS) {
        std::cerr << "设置 Content-Type 失败: " << std::endl;
        osip_message_free(msg);
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    result = osip_message_set_body(msg, response.c_str(), response.length());
    if (result != OSIP_SUCCESS) {
        std::cerr << "设置消息体失败: " << std::endl;
        osip_message_free(msg);
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    eXosip_message_send_request(_ex_context_ptr, msg);
    eXosip_unlock(_ex_context_ptr);
    std::cout << "MESSAGE 已发送" << std::endl;
    return true;
}

bool SipRegister::send_error_response(const int tid, const int status_code, const std::string& reason) const {
    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        return false;
    }

    if (tid <= 0) {
        std::cerr << "无效的事务ID: " << std::endl;
        return false;
    }

    std::cout << "发送错误响应: " << status_code << " " << reason.c_str() << std::endl;

    eXosip_lock(_ex_context_ptr);

    // 构建错误响应
    osip_message_t* error_response = nullptr;
    int result = eXosip_message_build_answer(_ex_context_ptr, tid, status_code, &error_response);
    if (result != OSIP_SUCCESS || !error_response) {
        std::cerr << "构建错误响应失败: " << std::endl;
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    if (!reason.empty() && error_response->reason_phrase) {
        osip_free(error_response->reason_phrase)
        error_response->reason_phrase = osip_strdup(reason.c_str());
    }

    // 发送响应
    result = eXosip_message_send_answer(_ex_context_ptr, tid, status_code, error_response);

    eXosip_unlock(_ex_context_ptr);

    if (result != OSIP_SUCCESS) {
        std::cerr << "发送错误响应失败: " << std::endl;
        return false;
    }

    std::cout << "错误响应已发送: " << status_code << " " << reason.c_str() << std::endl;
    return true;
}

void SipRegister::start_push_stream(const eXosip_event_t* event) {
    if (!event) {
        std::cerr << "事件对象为空" << std::endl;
        return;
    }

    if (!event->request) {
        std::cerr << "INVITE 请求为空" << std::endl;
        return;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  收到 INVITE 请求（点播推流）" << std::endl;
    std::cout << "│  Call ID: " << event->cid << std::endl;
    std::cout << "│  Dialog ID: " << event->did << std::endl;
    std::cout << "│  Transaction ID: " << event->tid << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    // 打印 INVITE 请求信息
    print_invite_info(event->request);

    const osip_body_t* body = nullptr;
    if (osip_list_size(&event->request->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->request->bodies, 0));
    }
    if (!body || !body->body || body->length == 0) {
        std::cerr << "INVITE 消息体为空" << std::endl;
        send_call_error_response(event->tid, 488, "INVITE 消息体为空");
        return;
    }
    const std::string sdp_offer(body->body, body->length);

    std::cout << "┌─────────── 平台 SDP Offer ───────────┐" << std::endl;
    std::cout << "" << sdp_offer.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    const auto sdp_struct = SdpParser::parse(sdp_offer);
    if (sdp_struct.remote_host.empty() || sdp_struct.remote_port == 0) {
        std::cerr << "SDP 解析失败：IP 或端口无效" << std::endl;
        std::cerr << "   Remote Host: " << sdp_struct.remote_host.c_str() << std::endl;
        std::cerr << "   Remote Port: " << sdp_struct.remote_port << std::endl;
        send_call_error_response(event->tid, 488, "SDP 解析失败：IP 或端口无效");
        return;
    }

    std::cout << "初始化 RTP 发送器..." << std::endl;
    if (sdp_struct.transport == "udp") {
        std::cerr << "当前传输协议为UDP，暂不支持" << std::endl;
        send_call_error_response(event->tid, 488, "当前传输协议为UDP，暂不支持");
        _event_callback(5001, "当前传输协议为UDP，暂不支持");
        return;
    }
    if (!RtpSender::get()->initialize(sdp_struct)) {
        std::cerr << "RTP 发送器初始化失败" << std::endl;
        send_call_error_response(event->tid, 500, "RTP 发送器初始化失败");
        _event_callback(5002, "RTP发送器初始化失败");
        return;
    }

    std::cout << "RTP 发送器初始化成功，构建 SDP Answer..." << std::endl;
    const std::string sdp_answer = SdpParser::buildUpstreamSdp(_device_code, _local_host, sdp_struct.ssrc);
    if (sdp_answer.empty()) {
        std::cerr << "构建 SDP Answer 失败" << std::endl;
        send_call_error_response(event->tid, 500, "构建 SDP Answer 失败");
        _event_callback(5003, "构建SDP应答失败");
        return;
    }

    eXosip_lock(_ex_context_ptr);

    // 构建 200 OK 响应
    osip_message_t* answer = nullptr;
    int ret = eXosip_call_build_answer(_ex_context_ptr, event->tid, 200, &answer);
    if (ret != OSIP_SUCCESS || !answer) {
        std::cerr << "构建 200 OK 失败: " << std::endl;
        eXosip_unlock(_ex_context_ptr);
        send_call_error_response(event->tid, 500, "构建200 OK响应失败");
        _event_callback(5004, "构建200 OK响应失败");
        return;
    }

    ret = osip_message_set_body(answer, sdp_answer.c_str(), sdp_answer.length());
    if (ret != OSIP_SUCCESS) {
        std::cerr << "设置消息体失败: " << std::endl;
        osip_message_free(answer);
        eXosip_unlock(_ex_context_ptr);
        send_call_error_response(event->tid, 500, "Internal Server Error");
        return;
    }

    ret = osip_message_set_content_type(answer, "application/sdp");
    if (ret != OSIP_SUCCESS) {
        std::cerr << "设置 Content-Type 失败: " << std::endl;
        osip_message_free(answer);
        eXosip_unlock(_ex_context_ptr);
        send_call_error_response(event->tid, 500, "Internal Server Error");
        return;
    }

    ret = eXosip_call_send_answer(_ex_context_ptr, event->tid, 200, answer);
    eXosip_unlock(_ex_context_ptr);
    if (ret != OSIP_SUCCESS) {
        std::cerr << "发送 200 OK 失败: " << std::endl;
        _event_callback(402, "发送200 OK响应失败");
        return;
    }

    _video_call_id = event->cid;
    _video_dialog_id = event->did;

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  200 OK 已发送" << std::endl;
    std::cout << "│  开始推送 H.264+G.711μ 流..." << std::endl;
    std::cout << "│  目标地址: " << sdp_struct.remote_host.c_str() << std::endl;
    std::cout << "│  目标端口: " << sdp_struct.remote_port << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    _event_callback(1000, "开始 H.264+G.711μ 推流");
}

bool SipRegister::send_call_error_response(const int tid, const int status_code, const std::string& reason) const {
    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        return false;
    }

    if (tid <= 0) {
        std::cerr << "无效的事务ID: " << std::endl;
        return false;
    }

    std::cout << "发送呼叫错误响应: " << status_code << " " << reason.c_str() << std::endl;

    eXosip_lock(_ex_context_ptr);

    // 构建错误响应
    osip_message_t* error_response = nullptr;
    int result = eXosip_call_build_answer(_ex_context_ptr, tid, status_code, &error_response);
    if (result != OSIP_SUCCESS || !error_response) {
        std::cerr << "构建错误响应失败: " << std::endl;
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    // 设置自定义的原因短语
    if (!reason.empty()) {
        if (error_response->reason_phrase) {
            osip_free(error_response->reason_phrase)
        }
        error_response->reason_phrase = osip_strdup(reason.c_str());
    }

    // 发送响应
    result = eXosip_call_send_answer(_ex_context_ptr, tid, status_code, error_response);

    eXosip_unlock(_ex_context_ptr);

    if (result != OSIP_SUCCESS) {
        std::cerr << "发送错误响应失败: " << std::endl;
        return false;
    }

    std::cout << "错误响应已发送: " << status_code << " " << reason.c_str() << std::endl;
    return true;
}

bool SipRegister::stop_push_stream() {
    if (_video_call_id <= 0) {
        std::cout << "没有正在进行的推流" << std::endl;
        return false;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  停止推流....                         │" << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    // 停止 RTP 发送器
    RtpSender::get()->stop();

    // 发送 BYE 请求
    if (_ex_context_ptr && _video_dialog_id > 0) {
        eXosip_lock(_ex_context_ptr);

        osip_message_t* bye = nullptr;
        const int ret = eXosip_call_build_request(_ex_context_ptr, _video_dialog_id, "BYE", &bye);
        if (ret == OSIP_SUCCESS && bye) {
            eXosip_call_send_request(_ex_context_ptr, _video_dialog_id, bye);
            std::cout << "BYE 请求已发送" << std::endl;
        }

        eXosip_unlock(_ex_context_ptr);
    }

    // 重置会话信息
    _video_call_id = -1;
    _video_dialog_id = -1;

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  推流已停止                           │" << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    _event_callback(1001, "停止 H.264+G.711μ 推流");
    return true;
}

void SipRegister::response_notify_sip(const pugi::xml_document& xml) {
    const pugi::xml_node notify_node = xml.child("Notify");
    if (!notify_node) {
        std::cerr << "XML 中未找到 Notify 节点" << std::endl;
        return;
    }

    const pugi::xml_node cmd_type_node = notify_node.child("CmdType");
    const pugi::xml_node sn_node = notify_node.child("SN");
    if (!cmd_type_node || !sn_node) {
        std::cerr << "缺少必要字段 (CmdType 或 SN)" << std::endl;
        return;
    }

    const std::string cmd_type = cmd_type_node.text().as_string();
    const std::string sn = sn_node.text().as_string();
    if (cmd_type.empty() || sn.empty()) {
        std::cerr << "CmdType 或 SN 为空" << std::endl;
        return;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  收到通知消息" << std::endl;
    std::cout << "│  CmdType: " << cmd_type.c_str() << std::endl;
    std::cout << "│  SN: " << sn.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

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
        std::cout << "处理语音广播通知" << std::endl;

        const pugi::xml_node source_id_node = notify_node.child("SourceID");
        const pugi::xml_node target_id_node = notify_node.child("TargetID");

        if (!source_id_node || !target_id_node) {
            std::cerr << "缺少 SourceID 或 TargetID" << std::endl;
            return;
        }

        const std::string source_id = source_id_node.text().as_string();
        const std::string target_id = target_id_node.text().as_string();

        if (source_id.empty() || target_id.empty()) {
            std::cerr << "SourceID 或 TargetID 为空" << std::endl;
            return;
        }

        std::cout << "初始化音频接收器..." << std::endl;
        std::lock_guard<std::mutex> lock(_audio_mutex);

        if (_audio_receiver_ptr) {
            std::cout << "停止旧的音频接收器" << std::endl;
            _audio_receiver_ptr->stop();
            _audio_receiver_ptr.reset();
        }

        _audio_receiver_ptr = std::make_shared<AudioReceiver>();
        if (!_audio_receiver_ptr) {
            std::cerr << "创建音频接收器失败" << std::endl;
            return;
        }

        const auto local_port = _audio_receiver_ptr->initialize();
        if (local_port < 0) {
            std::cerr << "初始化音频接收器失败" << std::endl;
            _audio_receiver_ptr.reset();
            return;
        }

        // 发送 INVITE
        if (send_audio_invite(source_id, target_id, local_port)) {
            std::cout << "语音广播 INVITE 已发送" << std::endl;
        } else {
            std::cerr << "发送语音广播 INVITE 失败" << std::endl;
            _audio_receiver_ptr->stop();
            _audio_receiver_ptr.reset();
        }
    } else {
        std::cerr << "不支持的通知类型: " << cmd_type.c_str() << std::endl;
    }
}

bool SipRegister::send_audio_invite(const std::string& source_id, const std::string& target_id,
                                    const uint16_t local_port) {
    if (source_id.empty() || target_id.empty()) {
        std::cerr << "SourceID 或 TargetID 为空" << std::endl;
        return false;
    }

    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        return false;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  发送音频 INVITE" << std::endl;
    std::cout << "│  From (设备): " << target_id.c_str() << std::endl;
    std::cout << "│  To (平台):   " << source_id.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    std::cout << "构建音频 SDP..." << std::endl;
    const std::string audio_sdp = SdpParser::buildDownstreamSdp(target_id, _local_host, local_port, false);
    if (audio_sdp.empty()) {
        std::cerr << "构建音频 SDP 失败" << std::endl;
        return false;
    }

    eXosip_lock(_ex_context_ptr);

    // 构建INVITE请求
    osip_message_t* invite = nullptr;
    int ret = eXosip_call_build_initial_invite(_ex_context_ptr,
                                               &invite,
                                               _to_uri.c_str(),
                                               _from_uri.c_str(),
                                               _proxy_uri.c_str(),
                                               nullptr);
    if (ret < 0 || !invite) {
        std::cerr << "构建音频 INVITE 失败: " << std::endl;
        eXosip_unlock(_ex_context_ptr);
        return false;
    }
    std::cout << "INVITE 请求已构建" << std::endl;

    /**
     * Subject 格式：SourceID:发送方序列号,TargetID:接收方序列号
     *
     * 示例：34020000002000000001:1,34020000001320000001:1
     * - 34020000002000000001: 平台（音频发送方）
     * - 1: 发送方媒体流序列号
     * - 34020000001320000001: 设备（音频接收方）
     * - 1: 接收方媒体流序列号
     */
    const std::string subject = source_id + ":1," + target_id + ":1";
    ret = osip_message_set_subject(invite, subject.c_str());
    if (ret != OSIP_SUCCESS) {
        std::cerr << "设置 Subject 失败: " << std::endl;
        osip_message_free(invite);
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    // 设置SDP body
    ret = osip_message_set_body(invite, audio_sdp.c_str(), audio_sdp.length());
    if (ret != OSIP_SUCCESS) {
        std::cerr << "设置消息体失败: " << std::endl;
        osip_message_free(invite);
        eXosip_unlock(_ex_context_ptr);
        return false;
    }

    ret = osip_message_set_content_type(invite, "application/sdp");
    if (ret != OSIP_SUCCESS) {
        std::cerr << "设置 Content-Type 失败: " << std::endl;
        osip_message_free(invite);
        eXosip_unlock(_ex_context_ptr);
        return false;
    }
    std::cout << "SDP body 已设置" << std::endl;

    // 发送INVITE
    const int call_id = eXosip_call_send_initial_invite(_ex_context_ptr, invite);
    eXosip_unlock(_ex_context_ptr);
    if (call_id < 0) {
        std::cerr << "发送音频 INVITE 失败: " << call_id << std::endl;
        return false;
    }

    _audio_call_id = call_id;

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  音频 INVITE 已发送" << std::endl;
    std::cout << "│  Call ID: " << call_id << std::endl;
    std::cout << "│  等待平台 200 OK 响应..." << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return true;
}

void SipRegister::start_receive_audio(const eXosip_event_t* event) {
    if (!event) {
        std::cerr << "事件对象为空" << std::endl;
        return;
    }

    if (!event->response) {
        std::cerr << "音频 INVITE 响应为空" << std::endl;
        return;
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  音频呼叫已应答" << std::endl;
    std::cout << "│  Call ID: " << event->cid << std::endl;
    std::cout << "│  Dialog ID: " << event->did << std::endl;
    std::cout << "│  Status Code: " << event->response->status_code << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    /**
     * 检查响应是否对应我们发送的音频 INVITE
     */
    if (event->cid != _audio_call_id.load()) {
        std::cerr << "Call ID 不匹配:" << std::endl;
        std::cerr << "   期望: " << _audio_call_id.load() << std::endl;
        std::cerr << "   实际: " << event->cid << std::endl;
        return;
    }
    _audio_dialog_id = event->did;

    // 解析平台返回的SDP，确认使用的编码
    const osip_body_t* body = nullptr;
    if (osip_list_size(&event->response->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->response->bodies, 0));
    }
    if (!body || !body->body || body->length == 0) {
        std::cerr << "音频响应消息体为空" << std::endl;
        return;
    }

    // 转换为字符串
    const std::string sdp_answer(body->body, body->length);
    std::cout << "┌────────── 平台 SDP Answer ──────────┐" << std::endl;
    std::cout << "" << sdp_answer.c_str() << std::endl;
    std::cout << "└─────────────────────────────────────┘" << std::endl;

    const auto audio_sdp_struct = SdpParser::parse(sdp_answer);
    if (!_audio_receiver_ptr->connectPlatform(audio_sdp_struct.remote_host, audio_sdp_struct.remote_port)) {
        std::cerr << "连接语音对讲平台失败" << std::endl;
        _event_callback(408, "连接语音对讲平台失败");
        return;
    }
    _event_callback(2000, "连接语音对讲成功，初始化麦克风");

    /**
     * GB28181 常用音频编码：
     * - Payload 8: PCMA (G.711 A-law)
     * - Payload 0: PCMU (G.711 μ-law)
     */
    std::string audio_codec = "Unknown";
    int payload_type = -1;
    if (audio_sdp_struct.rtp_map.find(8) != audio_sdp_struct.rtp_map.end()) {
        audio_codec = "PCMA (G.711 A-law)";
        payload_type = 8;
    } else if (audio_sdp_struct.rtp_map.find(0) != audio_sdp_struct.rtp_map.end()) {
        audio_codec = "PCMU (G.711 μ-law)";
        payload_type = 0;
    } else {
        std::cerr << "未识别的音频编码" << std::endl;
        if (!audio_sdp_struct.rtp_map.empty()) {
            payload_type = audio_sdp_struct.rtp_map.begin()->first;
            audio_codec = audio_sdp_struct.rtp_map.begin()->second;
        }
    }

    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  音频会话信息：" << std::endl;
    std::cout << "│  平台 IP: " << audio_sdp_struct.remote_host.c_str() << std::endl;
    std::cout << "│  平台端口: " << audio_sdp_struct.remote_port << std::endl;
    std::cout << "│  音频编码: " << audio_codec.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    std::cout << "发送 ACK 确认..." << std::endl;
    if (!_ex_context_ptr) {
        std::cerr << "eXosip 上下文为空" << std::endl;
        return;
    }
    eXosip_lock(_ex_context_ptr);

    osip_message_t* ack = nullptr;
    const int ret = eXosip_call_build_ack(_ex_context_ptr, event->did, &ack);
    if (ret == OSIP_SUCCESS && ack) {
        eXosip_call_send_ack(_ex_context_ptr, event->did, ack);
        std::cout << "ACK 已发送" << std::endl;
    }
    eXosip_unlock(_ex_context_ptr);

    std::cout << "等待平台发送音频流..." << std::endl;
    _audio_receiver_ptr->start([this, payload_type](const uint8_t* buffer, const size_t len) -> void {
        const auto g711_buffer = reinterpret_cast<const int8_t*>(buffer);
        const std::vector<int8_t> g711(g711_buffer, g711_buffer + len);
        _g711_callback(g711, len);
        /**
        * G.711 编码:  int16_t(2字节) → uint8_t(1字节)  // 2:1压缩
        * G.711 解码:  uint8_t(1字节) → int16_t(2字节)  // 1:1扩展
        * len 既是字节数，也是采样数，所以 pcm_buffer(len) 正好分配所需空间
        * */
        std::vector<int16_t> pcm_buffer(len);
        if (payload_type == 8) {
            AudioProcessor::alaw_to_pcm(buffer, pcm_buffer.data(), len);
        } else {
            AudioProcessor::ulaw_to_pcm(buffer, pcm_buffer.data(), len);
        }
        _pcm_callback(pcm_buffer, len);
    });
}

bool SipRegister::stop_receive_audio() {
    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  停止音频接收                         │" << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    std::lock_guard<std::mutex> lock(_audio_mutex);

    // 停止音频接收器
    if (_audio_receiver_ptr) {
        _audio_receiver_ptr->stop();
        _audio_receiver_ptr.reset();
        std::cout << "音频接收器已停止" << std::endl;
    } else {
        std::cout << "没有正在运行的音频接收器" << std::endl;
        return false;
    }

    // 发送 BYE 请求
    if (_ex_context_ptr && _audio_dialog_id.load() > 0) {
        std::cout << "发送 BYE 请求..." << std::endl;

        eXosip_lock(_ex_context_ptr);

        osip_message_t* bye = nullptr;
        int ret = eXosip_call_build_request(_ex_context_ptr, _audio_dialog_id.load(), "BYE", &bye);
        if (ret == OSIP_SUCCESS && bye) {
            ret = eXosip_call_send_request(_ex_context_ptr, _audio_dialog_id.load(), bye);
            if (ret == OSIP_SUCCESS) {
                std::cout << "BYE 请求已发送" << std::endl;
            } else {
                std::cout << "发送 BYE 失败: " << std::endl;
            }
        } else {
            std::cerr << "构建 BYE 失败: " << std::endl;
        }

        eXosip_unlock(_ex_context_ptr);
    }

    // 重置会话信息
    _audio_call_id = -1;
    _audio_dialog_id = -1;

    std::cout << "音频接收已停止" << std::endl;
    _event_callback(2001, "停止接收音频流");
    return true;
}

std::string SipRegister::current_register_state() const {
    switch (_reg_state.load()) {
        case REG_STATE_IDLE:
            return "idle";
        case REG_STATE_SENT_INITIAL:
            return "sent_initial";
        case REG_STATE_SENT_AUTH:
            return "sent_auth";
        case REG_STATE_SUCCESS:
            return "success";
        case REG_STATE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}
