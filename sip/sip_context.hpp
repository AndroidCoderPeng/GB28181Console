//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_SIP_CONTEXT_HPP
#define GB28181CONSOLE_SIP_CONTEXT_HPP

#include <eXosip2/eX_setup.h>

#include "logger.hpp"
#include "sip.hpp"

class SipContext {
public:
    explicit SipContext(const Sip::SipParameter &param);

    ~SipContext();

    bool initialize();

    void destroy();

    eXosip_t *getContextPtr() const { return _ex_context_ptr; }

    bool isValid() const { return _ex_context_ptr != nullptr; }

    // URI 获取
    const std::string &getFromUri() const { return _from_uri; }

    const std::string &getToUri() const { return _to_uri; }

    const std::string &getProxyUri() const { return _proxy_uri; }

    const Sip::SipParameter &getSipParameter() const { return _parameter; }

    // 线程安全锁
    void lock() const;

    void unlock() const;

private:
    Logger _logger;
    Sip::SipParameter _parameter;
    eXosip_t *_ex_context_ptr = nullptr;

    std::string _from_uri;
    std::string _to_uri;
    std::string _proxy_uri;
};

#endif //GB28181CONSOLE_SIP_CONTEXT_HPP