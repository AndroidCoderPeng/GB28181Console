//
// Created by pengx on 2026/3/10.
//

#include "stream_manager.hpp"

#include "response_sender.hpp"
#include "rtp_sender.hpp"
#include "state_code.hpp"
#include "audio/audio_processor.hpp"

StreamManager::StreamManager(SipContext* context, IStreamObserver* observer) : _logger("StreamManager"),
                                                                               _sip_context_ptr(context),
                                                                               _stream_observer_ptr(observer) {
    _logger.i("StreamManager created");
}

void StreamManager::handleVideoInvite(eXosip_event_t* event) {
    if (!event) {
        _logger.e("事件对象为空");
        return;
    }

    if (!event->request) {
        _logger.e("INVITE 请求为空");
        return;
    }

    auto box = _logger.dBox().add("收到 INVITE 请求（点播推流）");
    osip_header_t* subject = nullptr;
    osip_message_get_subject(event->request, 0, &subject);
    if (subject && subject->hvalue) {
        box.addFmt("Subject: %s", subject->hvalue);
    }
    box.addFmt("Call ID: %d", event->cid)
       .addFmt("Dialog ID: %d", event->did)
       .addFmt("Transaction ID: %d", event->tid)
       .print();

    osip_body_t* body = nullptr;
    if (osip_list_size(&event->request->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->request->bodies, 0));
    }
    if (!body || !body->body || body->length == 0) {
        _logger.e("INVITE 消息体为空");
        send_sip_call_error_response(event->tid, 488, "INVITE 消息体为空");
        return;
    }
    std::string sdp_offer(body->body, body->length);
    _logger.d("平台 SDP Offer");
    _logger.dBox().addBlock(sdp_offer).print();

    auto sdp_struct = SdpParser::get()->parse(sdp_offer);
    if (sdp_struct.remote_host.empty() || sdp_struct.remote_port == 0) {
        _logger.eBox()
               .add("SDP 解析失败：IP 或端口无效")
               .addFmt("Remote Host: %s", sdp_struct.remote_host.c_str())
               .addFmt("Remote Port: %d", sdp_struct.remote_port)
               .print();
        send_sip_call_error_response(event->tid, 488, "SDP 解析失败：IP 或端口无效");
        return;
    }

    _logger.i("初始化 RTP 发送器...");
    bool init_socket_success = false;
    if (sdp_struct.transport == "udp") {
        init_socket_success = RtpSender::get()->initUdpSocket(sdp_struct);
    } else {
        init_socket_success = RtpSender::get()->initTcpSocket(sdp_struct);
    }
    if (!init_socket_success) {
        _stream_observer_ptr->onStreamStateChanged(2109, StateCode::toString(2109));
        send_sip_call_error_response(event->tid, 500, StateCode::toString(2109));
        return;
    }

    _logger.i("RTP 发送器初始化成功，构建 SDP Answer...");
    const auto parameter = _sip_context_ptr->getSipParameter();
    std::string sdp_answer = SdpParser::get()->buildUpstreamSdp(parameter.deviceCode,
                                                                parameter.localHost,
                                                                sdp_struct.ssrc);
    if (sdp_answer.empty()) {
        _stream_observer_ptr->onStreamStateChanged(2103, StateCode::toString(2103));
        send_sip_call_error_response(event->tid, 500, StateCode::toString(2103));
        return;
    }
    _sip_context_ptr->lock();

    // 构建 200 OK 响应
    osip_message_t* answer = nullptr;
    eXosip_call_build_answer(_sip_context_ptr->getContextPtr(), event->tid, 200, &answer);
    osip_message_set_body(answer, sdp_answer.c_str(), sdp_answer.length());
    osip_message_set_content_type(answer, "application/sdp");
    eXosip_call_send_answer(_sip_context_ptr->getContextPtr(), event->tid, 200, answer);
    _sip_context_ptr->unlock();

    _video_call_id = event->cid;
    _video_dialog_id = event->did;

    _logger.dBox()
           .add("200 OK 已发送")
           .add("开始推送 H.264+G.711μ 流...")
           .addFmt("目标地址: %s", sdp_struct.remote_host.c_str())
           .addFmt("目标端口: %d", sdp_struct.remote_port)
           .print();
    _stream_observer_ptr->onStreamStateChanged(2100, StateCode::toString(2100));
}

bool StreamManager::stopPushStream() {
    if (_video_call_id <= 0) {
        return false;
    }

    _logger.i("停止推流....");

    // 停止 RTP 发送器
    RtpSender::get()->stop();

    // 发送 BYE 请求
    if (_sip_context_ptr->isValid() && _video_dialog_id > 0) {
        _sip_context_ptr->lock();

        osip_message_t* bye = nullptr;
        eXosip_call_build_request(_sip_context_ptr->getContextPtr(), _video_dialog_id, "BYE", &bye);
        if (bye) {
            eXosip_call_send_request(_sip_context_ptr->getContextPtr(), _video_dialog_id, bye);
            _logger.i("BYE 请求已发送");
        }

        _sip_context_ptr->unlock();
    }

    // 重置会话信息
    _video_call_id = -1;
    _video_dialog_id = -1;

    _logger.i("推流已停止");
    _stream_observer_ptr->onStreamStateChanged(2101, StateCode::toString(2101));
    return true;
}

void StreamManager::initAudioReceiver(const std::string& source_id, const std::string& target_id) {
    std::lock_guard<std::mutex> lock(_audio_mutex);

    if (_audio_receiver_ptr) {
        _logger.i("停止旧的音频接收器");
        _audio_receiver_ptr->stop();
        _audio_receiver_ptr.reset();
    }

    _audio_receiver_ptr = std::make_unique<AudioReceiver>();
    if (!_audio_receiver_ptr) {
        _logger.e("创建音频接收器失败");
        return;
    }

    const auto local_port = _audio_receiver_ptr->initialize();
    if (local_port < 0) {
        _logger.e("初始化音频接收器失败");
        _audio_receiver_ptr.reset();
        return;
    }

    // 发送 INVITE
    const auto sender = ResponseSender::get();
    _audio_call_id = sender->sendAudioInvite(_sip_context_ptr, source_id, target_id, local_port,
                                             [this](const int code, const std::string& message) {
                                                 _stream_observer_ptr->onStreamStateChanged(code, message);
                                             });
    if (_audio_call_id < 0) {
        _logger.e("发送语音广播 INVITE 失败");
        _audio_receiver_ptr->stop();
        _audio_receiver_ptr.reset();
    } else {
        _logger.i("语音广播 INVITE 已发送");
    }
}

void StreamManager::handleAudioAnswer(const eXosip_event_t* event) {
    if (!event) {
        _logger.e("事件对象为空");
        return;
    }

    if (!event->response) {
        _logger.e("音频 INVITE 响应为空");
        return;
    }

    _logger.dBox()
           .add("音频呼叫已应答")
           .addFmt("Call ID: %d", event->cid)
           .addFmt("Dialog ID: %d", event->did)
           .addFmt("Status Code: %d", event->response->status_code)
           .print();

    /**
     * 检查响应是否对应我们发送的音频 INVITE
     */
    if (event->cid != _audio_call_id.load()) {
        _logger.eBox()
               .add("Call ID 不匹配:")
               .addFmt("期望: %d", _audio_call_id.load())
               .addFmt("实际: %d", event->cid)
               .print();
        return;
    }
    _audio_dialog_id = event->did;

    // 解析平台返回的SDP，确认使用的编码
    const osip_body_t* body = nullptr;
    if (osip_list_size(&event->response->bodies) > 0) {
        body = static_cast<osip_body_t*>(osip_list_get(&event->response->bodies, 0));
    }
    if (!body || !body->body || body->length == 0) {
        _logger.e("音频响应消息体为空");
        return;
    }

    // 转换为字符串
    const std::string sdp_answer(body->body, body->length);
    _logger.d("平台 SDP Answer");
    _logger.dBox().addBlock(sdp_answer).print();

    const auto audio_sdp_struct = SdpParser::get()->parse(sdp_answer);
    if (!_audio_receiver_ptr->connectPlatform(audio_sdp_struct.remote_host,
                                              audio_sdp_struct.remote_port)) {
        _stream_observer_ptr->onStreamStateChanged(2203, StateCode::toString(2203));
        return;
    }
    _stream_observer_ptr->onStreamStateChanged(2200, StateCode::toString(2200));

    /**
     * GB28181 常用音频编码：
     * - Payload 8: PCMA (G.711 A-law)
     * - Payload 0: PCMU (G.711 μ-law)
     */
    int decode_type = -1;
    std::string audio_codec = "Unknown";
    if (audio_sdp_struct.rtp_map.find(8) != audio_sdp_struct.rtp_map.end()) {
        decode_type = 8;
        audio_codec = "PCMA (G.711 A-law)";
    } else if (audio_sdp_struct.rtp_map.find(0) != audio_sdp_struct.rtp_map.end()) {
        decode_type = 0;
        audio_codec = "PCMU (G.711 μ-law)";
    } else {
        _logger.w("未识别的音频编码");
        if (!audio_sdp_struct.rtp_map.empty()) {
            decode_type = audio_sdp_struct.rtp_map.begin()->first;
            audio_codec = audio_sdp_struct.rtp_map.begin()->second;
        }
    }

    _logger.dBox()
           .add("音频会话信息：")
           .addFmt("平台 IP: %s", audio_sdp_struct.remote_host.c_str())
           .addFmt("平台端口: %d", audio_sdp_struct.remote_port)
           .addFmt("解码类型: %d", decode_type)
           .addFmt("音频编码: %s", audio_codec.c_str())
           .print();

    if (!_sip_context_ptr->isValid()) {
        _logger.e("eXosip 上下文为空");
        return;
    }
    _sip_context_ptr->lock();

    osip_message_t* ack = nullptr;
    eXosip_call_build_ack(_sip_context_ptr->getContextPtr(), event->did, &ack);
    if (ack) {
        eXosip_call_send_ack(_sip_context_ptr->getContextPtr(), event->did, ack);
        _logger.i("ACK 已发送");
    }
    _sip_context_ptr->unlock();

    _logger.i("等待平台发送音频流...");
    thread_local std::vector<int16_t> pcm_buffer;
    _audio_receiver_ptr->start([this, decode_type](uint8_t* buffer, size_t len) -> void {
        _stream_observer_ptr->onG711DataReceived(buffer, len);
        /**
         * len 就是 recv() 返回的接收字节数。
         *
         * G.711A 是8位采样，每个采样占1字节，所以采样数 = 字节数
         *
         * PCM 是16位采样，每个采样点 2 字节，乘以 sizeof(uint16_t) 才能得到字节数
         * */
        pcm_buffer.resize(len); // 分配PCM缓冲区（需要len个int16_t空间）
        if (decode_type == 8) {
            AudioProcessor::alaw_to_pcm(buffer, pcm_buffer.data(), len);
        } else {
            AudioProcessor::ulaw_to_pcm(buffer, pcm_buffer.data(), len);
        }
        _stream_observer_ptr->onPcmDataReceived(pcm_buffer.data(), len);
    });
}

bool StreamManager::stopReceiveAudio() {
    _logger.i("停止音频接收....");

    std::lock_guard<std::mutex> lock(_audio_mutex);

    // 停止音频接收器
    if (_audio_receiver_ptr) {
        _audio_receiver_ptr->stop();
        _audio_receiver_ptr.reset();
        _logger.i("音频接收器已停止");
    } else {
        return false;
    }

    // 发送 BYE 请求
    if (_sip_context_ptr->isValid() && _audio_dialog_id.load() > 0) {
        _logger.i("发送 BYE 请求...");

        _sip_context_ptr->lock();

        osip_message_t* bye = nullptr;
        eXosip_call_build_request(_sip_context_ptr->getContextPtr(),
                                  _audio_dialog_id.load(),
                                  "BYE",
                                  &bye);
        if (bye) {
            const int ret = eXosip_call_send_request(_sip_context_ptr->getContextPtr(),
                                                     _audio_dialog_id.load(),
                                                     bye);
            if (ret == OSIP_SUCCESS) {
                _logger.i("BYE 请求已发送");
            } else {
                _logger.eFmt("发送 BYE 失败: %d", ret);
            }
        }
        _sip_context_ptr->unlock();
    }

    // 重置会话信息
    _audio_call_id = -1;
    _audio_dialog_id = -1;

    _logger.i("音频接收已停止");
    _stream_observer_ptr->onStreamStateChanged(2201, StateCode::toString(2201));
    return true;
}

void StreamManager::callClosed(int cid) {
    if (cid == _audio_call_id.load()) {
        _logger.i("音频对讲结束");
        stopReceiveAudio();
    } else if (cid == _video_call_id.load()) {
        _logger.i("视频推流结束");
        stopPushStream();
    }
}

void StreamManager::reset() {
    _video_call_id = -1;
    _video_dialog_id = -1;
    _audio_call_id = -1;
    _audio_dialog_id = -1;
}

// ============================================================
// 给平台发送消息的相关函数具体实现
// ============================================================
bool StreamManager::send_sip_call_error_response(const int tid, const int code, const std::string& reason) const {
    return ResponseSender::get()->sendCallErrorResponse(_sip_context_ptr, tid, code, reason);
}