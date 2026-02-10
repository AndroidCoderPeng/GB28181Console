//
// Created by pengx on 2026/2/10.
//

#include "error_code.hpp"

std::string ErrorCode::toString(const int code) {
    switch (code) {
        // 成功
        case 200:
            return "OK";
        case 201:
            return "注销成功";

        // 临时响应
        case 100:
            return "Trying";
        case 180:
            return "Ringing";
        case 181:
            return "Call Is Being Forwarded";
        case 182:
            return "Queued";
        case 183:
            return "Session Progress";

        // 重定向
        case 300:
            return "Multiple Choices";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Moved Temporarily";

        // 客户端错误
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 402:
            return "Payment Required";
        case 403:
            return "注册被拒绝 (403 Forbidden)";
        case 404:
            return "服务器未找到 (404 Not Found)";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 407:
            return "Proxy Authentication Required";
        case 408:
            return "请求超时 (408 Request Timeout)";
        case 410:
            return "Gone";
        case 413:
            return "Request Entity Too Large";
        case 414:
            return "Request-URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Unsupported URI Scheme";
        case 420:
            return "Bad Extension";
        case 421:
            return "Extension Required";
        case 423:
            return "注册间隔太短 (423 Interval Too Brief)";
        case 480:
            return "服务暂时不可用 (480 Temporarily Unavailable)";
        case 481:
            return "Call/Transaction Does Not Exist";
        case 482:
            return "Loop Detected";
        case 483:
            return "Too Many Hops";
        case 484:
            return "Address Incomplete";
        case 485:
            return "Ambiguous";
        case 486:
            return "Busy Here";
        case 487:
            return "Request Terminated";
        case 488:
            return "Not Acceptable Here";
        case 491:
            return "Request Pending";
        case 493:
            return "Undecipherable";

        // 服务器错误
        case 500:
            return "服务器内部错误 (500 Server Internal Error)";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "服务不可用 (503 Service Unavailable)";
        case 504:
            return "Server Time-out";
        case 505:
            return "Version Not Supported";
        case 513:
            return "Message Too Large";

        // 全局失败
        case 600:
            return "Busy Everywhere";
        case 603:
            return "Decline";
        case 604:
            return "Does Not Exist Anywhere";
        case 606:
            return "Not Acceptable";

        // 自定义错误码
        case 1000:
            return "开始推流";
        case 1001:
            return "停止推流";
        case 2000:
            return "开始接收音频";
        case 2001:
            return "停止接收音频";

        case 4011:
            return "构建注册请求失败";
        case 4012:
            return "发送注册请求失败";
        case 4013:
            return "构建认证注册请求失败";
        case 4014:
            return "发送认证注册请求失败";
        case 4021:
            return "构建注销请求失败";
        case 4022:
            return "发送注销请求失败";

        default:
            return "Unknown Error (" + std::to_string(code) + ")";
    }
}
