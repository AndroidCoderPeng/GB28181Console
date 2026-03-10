//
// Created by pengx on 2026/3/10.
//

#include "sip_context.hpp"

#include <netinet/in.h>

SipContext::SipContext(const Sip::SipParameter& param) : _logger("SipContext"), _parameter(param) {
    _from_uri = "sip:" + param.deviceCode + "@" + param.serverDomain;
    _to_uri = "sip:" + param.serverCode + "@" + param.serverDomain;
    _proxy_uri = "sip:" + param.serverHost + ":" + std::to_string(param.serverPort);

    _logger.dBox()
           .add("SipContext created")
           .addFmt("From: %s", _from_uri.c_str())
           .addFmt("To: %s", _to_uri.c_str())
           .addFmt("Proxy: %s", _proxy_uri.c_str())
           .print();
}

SipContext::~SipContext() {
    destroy();
}

bool SipContext::initialize() {
    _ex_context_ptr = eXosip_malloc();
    if (!_ex_context_ptr) {
        _logger.e("eXosip_malloc 失败");
        return false;
    }

    int ret = eXosip_init(_ex_context_ptr);
    if (ret != OSIP_SUCCESS) {
        _logger.e("eXosip_init 失败");
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        return false;
    }

    /**
     * 监听本地端口，用于接收Sip信令，与流传输方式并无关联
     * */
    ret = eXosip_listen_addr(_ex_context_ptr,
                             IPPROTO_TCP,
                             nullptr,
                             5060,
                             AF_INET,
                             0);
    if (ret != OSIP_SUCCESS) {
        eXosip_quit(_ex_context_ptr);
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        return false;
    }

    // 设置用户代理。User-Agent 用于标识 SIP 客户端的身份信息，类似于 HTTP 协议中的 User-Agent。
    const std::string user_agent = "GB28181-Device/1.0 " + _parameter.deviceName;
    eXosip_set_user_agent(_ex_context_ptr, user_agent.c_str());

    _logger.i("SipContext 初始化成功");
    return true;
}

void SipContext::destroy() {
    if (_ex_context_ptr) {
        eXosip_quit(_ex_context_ptr);
        osip_free(_ex_context_ptr)
        _ex_context_ptr = nullptr;
        _logger.i("SipContext 已销毁");
    }
}

void SipContext::lock() const {
    if (_ex_context_ptr) {
        eXosip_lock(_ex_context_ptr);
    }
}

void SipContext::unlock() const {
    if (_ex_context_ptr) {
        eXosip_unlock(_ex_context_ptr);
    }
}