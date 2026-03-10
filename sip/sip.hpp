//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_SIP_HPP
#define GB28181CONSOLE_SIP_HPP

#include <string>

namespace Sip {
    /**
     * SipParameter 数据结构
     */
    struct SipParameter {
        std::string localHost;
        std::string serverHost;
        int serverPort;
        std::string serverCode;
        std::string serverDomain;
        std::string deviceCode;
        std::string serialNumber;
        std::string deviceName;
        std::string password;
        double longitude;
        double latitude;
    };

    // 注册状态
    enum class RegisterState {
        IDLE,
        SENT_INITIAL,
        SENT_AUTH,
        SUCCESS,
        FAILED
    };
}

#endif //GB28181CONSOLE_SIP_HPP
