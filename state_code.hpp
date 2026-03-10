//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_STATE_CODE_HPP
#define GB28181CONSOLE_STATE_CODE_HPP

#include <string>

class StateCode {
public:
    static std::string toString(int code) {
        switch (code) {
            // ==========================================
            // 1xx 临时响应 (Provisional)
            // ==========================================
            case 100:
                return "Trying (正在尝试)";
            case 180:
                return "Ringing (正在振铃)";
            case 181:
                return "Call Is Being Forwarded (呼叫正在转发)";
            case 182:
                return "Queued (已排队)";
            case 183:
                return "Session Progress (会话进行中)";

            // ==========================================
            // 2xx 成功响应 (Success)
            // ==========================================
            case 200:
                return "OK (请求成功)";
            case 201:
                return "Unregistration Success (注销成功)";

            // ==========================================
            // 3xx 重定向响应 (Redirection)
            // ==========================================
            case 300:
                return "Multiple Choices (多重选择)";
            case 301:
                return "Moved Permanently (永久移动)";
            case 302:
                return "Moved Temporarily (临时移动)";
            case 305:
                return "Use Proxy (使用代理)";

            // ==========================================
            // 4xx 客户端错误 (Client Error)
            // ==========================================
            case 400:
                return "Bad Request (请求格式错误)";
            case 401:
                return "Unauthorized (未授权，需要认证)";
            case 402:
                return "Payment Required (需要付费)";
            case 403:
                return "Forbidden (服务器拒绝请求)";
            case 404:
                return "Not Found (用户/设备不存在)";
            case 405:
                return "Method Not Allowed (方法不允许)";
            case 406:
                return "Not Acceptable (不可接受)";
            case 407:
                return "Proxy Authentication Required (需要代理认证)";
            case 408:
                return "Request Timeout (请求超时)";
            case 410:
                return "Gone (资源已消失)";
            case 413:
                return "Request Entity Too Large (请求实体过大)";
            case 414:
                return "Request-URI Too Long (请求URI过长)";
            case 415:
                return "Unsupported Media Type (不支持的媒体类型)";
            case 416:
                return "Unsupported URI Scheme (不支持的URI方案)";
            case 420:
                return "Bad Extension (错误的扩展)";
            case 421:
                return "Extension Required (需要扩展)";
            case 423:
                return "Interval Too Brief (注册间隔太短)";
            case 480:
                return "Temporarily Unavailable (服务暂时不可用)";
            case 481:
                return "Call/Transaction Does Not Exist (呼叫/事务不存在)";
            case 482:
                return "Loop Detected (检测到循环)";
            case 483:
                return "Too Many Hops (跳数过多)";
            case 484:
                return "Address Incomplete (地址不完整)";
            case 485:
                return "Ambiguous (地址歧义)";
            case 486:
                return "Busy Here (此处忙)";
            case 487:
                return "Request Terminated (请求已终止)";
            case 488:
                return "Not Acceptable Here (此处不可接受)";
            case 491:
                return "Request Pending (请求挂起)";
            case 493:
                return "Undecipherable (无法解析，加密错误)";

            // ==========================================
            // 5xx 服务器错误 (Server Error)
            // ==========================================
            case 500:
                return "Server Internal Error (服务器内部错误)";
            case 501:
                return "Not Implemented (功能未实现)";
            case 502:
                return "Bad Gateway (网关错误)";
            case 503:
                return "Service Unavailable (服务不可用)";
            case 504:
                return "Server Time-out (服务器超时)";
            case 505:
                return "Version Not Supported (SIP版本不支持)";
            case 513:
                return "Message Too Large (消息过大)";

            // ==========================================
            // 6xx 全局失败 (Global Failure)
            // ==========================================
            case 600:
                return "Busy Everywhere (各处忙)";
            case 603:
                return "Decline (拒绝)";
            case 604:
                return "Does Not Exist Anywhere (到处不存在)";
            case 606:
                return "Not Acceptable (全局不可接受)";

            // ==========================================
            // 1000-1099: exosip2 事件类型
            // ==========================================
            case 1000:
                return "EXOSIP_REGISTRATION_SUCCESS (注册成功)";
            case 1001:
                return "EXOSIP_REGISTRATION_FAILURE (注册失败)";
            case 1002:
                return "EXOSIP_REGISTRATION_REFRESHED (注册已刷新)";
            case 1003:
                return "EXOSIP_REGISTRATION_TERMINATED (注册已终止)";

            case 1010:
                return "EXOSIP_CALL_INVITE (收到INVITE呼叫)";
            case 1011:
                return "EXOSIP_CALL_REINVITE (收到重协商请求)";
            case 1012:
                return "EXOSIP_CALL_NOANSWER (呼叫无应答)";
            case 1013:
                return "EXOSIP_CALL_PROCEEDING (呼叫处理中)";
            case 1014:
                return "EXOSIP_CALL_RINGING (正在振铃)";
            case 1015:
                return "EXOSIP_CALL_ANSWERED (呼叫已应答)";
            case 1016:
                return "EXOSIP_CALL_REDIRECTED (呼叫被重定向)";
            case 1017:
                return "EXOSIP_CALL_REQUESTFAILURE (请求失败)";
            case 1018:
                return "EXOSIP_CALL_SERVERFAILURE (服务器失败)";
            case 1019:
                return "EXOSIP_CALL_GLOBALFAILURE (全局失败)";
            case 1020:
                return "EXOSIP_CALL_ACK (收到ACK)";
            case 1021:
                return "EXOSIP_CALL_CANCELLED (呼叫已取消)";
            case 1022:
                return "EXOSIP_CALL_MESSAGE_NEW (收到新的呼叫内消息)";
            case 1023:
                return "EXOSIP_CALL_MESSAGE_PROCEEDING (消息处理中)";
            case 1024:
                return "EXOSIP_CALL_MESSAGE_ANSWERED (消息已应答)";
            case 1025:
                return "EXOSIP_CALL_MESSAGE_REDIRECTED (消息被重定向)";
            case 1026:
                return "EXOSIP_CALL_MESSAGE_REQUESTFAILURE (消息请求失败)";
            case 1027:
                return "EXOSIP_CALL_MESSAGE_SERVERFAILURE (消息服务器失败)";
            case 1028:
                return "EXOSIP_CALL_MESSAGE_GLOBALFAILURE (消息全局失败)";
            case 1029:
                return "EXOSIP_CALL_CLOSED (呼叫已关闭)";
            case 1030:
                return "EXOSIP_CALL_RELEASED (呼叫已释放)";

            case 1040:
                return "EXOSIP_MESSAGE_NEW (收到新的MESSAGE请求)";
            case 1041:
                return "EXOSIP_MESSAGE_PROCEEDING (消息处理中)";
            case 1042:
                return "EXOSIP_MESSAGE_ANSWERED (消息已应答)";
            case 1043:
                return "EXOSIP_MESSAGE_REDIRECTED (消息被重定向)";
            case 1044:
                return "EXOSIP_MESSAGE_REQUESTFAILURE (消息请求失败)";
            case 1045:
                return "EXOSIP_MESSAGE_SERVERFAILURE (消息服务器失败)";
            case 1046:
                return "EXOSIP_MESSAGE_GLOBALFAILURE (消息全局失败)";

            case 1050:
                return "EXOSIP_SUBSCRIPTION_NOANSWER (订阅无应答)";
            case 1051:
                return "EXOSIP_SUBSCRIPTION_PROCEEDING (订阅处理中)";
            case 1052:
                return "EXOSIP_SUBSCRIPTION_ANSWERED (订阅已应答)";
            case 1053:
                return "EXOSIP_SUBSCRIPTION_REDIRECTED (订阅被重定向)";
            case 1054:
                return "EXOSIP_SUBSCRIPTION_REQUESTFAILURE (订阅请求失败)";
            case 1055:
                return "EXOSIP_SUBSCRIPTION_SERVERFAILURE (订阅服务器失败)";
            case 1056:
                return "EXOSIP_SUBSCRIPTION_GLOBALFAILURE (订阅全局失败)";
            case 1057:
                return "EXOSIP_SUBSCRIPTION_NOTIFY (收到NOTIFY通知)";
            case 1058:
                return "EXOSIP_SUBSCRIPTION_RELEASED (订阅已释放)";

            case 1060:
                return "EXOSIP_NOTIFICATION_NOANSWER (通知无应答)";
            case 1061:
                return "EXOSIP_NOTIFICATION_PROCEEDING (通知处理中)";
            case 1062:
                return "EXOSIP_NOTIFICATION_ANSWERED (通知已应答)";
            case 1063:
                return "EXOSIP_NOTIFICATION_REDIRECTED (通知被重定向)";
            case 1064:
                return "EXOSIP_NOTIFICATION_REQUESTFAILURE (通知请求失败)";
            case 1065:
                return "EXOSIP_NOTIFICATION_SERVERFAILURE (通知服务器失败)";
            case 1066:
                return "EXOSIP_NOTIFICATION_GLOBALFAILURE (通知全局失败)";

            case 1070:
                return "EXOSIP_IN_SUBSCRIPTION_NEW (收到新的订阅请求)";

            case 1080:
                return "EXOSIP_KEEPALIVE (保活事件)";

            // ==========================================
            // 1100-1199: osip2 返回码
            // ==========================================
            case 1100:
                return "OSIP_SUCCESS (操作成功)";
            case 1101:
                return "OSIP_UNDEFINED_ERROR (未定义错误)";
            case 1102:
                return "OSIP_BADPARAMETER (参数错误)";
            case 1103:
                return "OSIP_WRONG_STATE (状态错误)";
            case 1104:
                return "OSIP_NOMEM (内存不足)";
            case 1105:
                return "OSIP_SYNTAXERROR (语法错误)";
            case 1106:
                return "OSIP_NOTFOUND (未找到)";
            case 1107:
                return "OSIP_API_NOT_INITIALIZED (API未初始化)";
            case 1108:
                return "OSIP_NO_NETWORK (网络错误)";
            case 1109:
                return "OSIP_PORT_BUSY (端口被占用)";
            case 1110:
                return "OSIP_UNKNOWN_HOST (未知主机)";
            case 1111:
                return "OSIP_DISK_FULL (磁盘已满)";
            case 1112:
                return "OSIP_NO_COMMON_CIPHER (无通用加密算法)";
            case 1113:
                return "OSIP_TIMEOUT (操作超时)";
            case 1114:
                return "OSIP_NO_ERROR (无错误)";
            case 1115:
                return "OSIP_CANCELLED (操作已取消)";
            case 1116:
                return "OSIP_DECLINE (操作被拒绝)";
            case 1117:
                return "OSIP_ABORTED (操作已中止)";
            case 1118:
                return "OSIP_NOTIMPLEMENTED (功能未实现)";
            case 1119:
                return "OSIP_NO_MEMORY (内存分配失败)";
            case 1120:
                return "OSIP_CONNECTION_RESET (连接被重置)";
            case 1121:
                return "OSIP_CONNECTION_REFUSED (连接被拒绝)";
            case 1122:
                return "OSIP_CONNECTION_TIMEOUT (连接超时)";

            // ==========================================
            // 2000-2099: GB28181 注册相关错误码
            // ==========================================
            case 2000:
                return "GB_REGISTER_BUILD_FAILED (构建注册请求失败)";
            case 2001:
                return "GB_REGISTER_SEND_FAILED (发送注册请求失败)";
            case 2002:
                return "GB_REGISTER_AUTH_BUILD_FAILED (构建认证注册请求失败)";
            case 2003:
                return "GB_REGISTER_AUTH_SEND_FAILED (发送认证注册请求失败)";
            case 2004:
                return "GB_UNREGISTER_BUILD_FAILED (构建注销请求失败)";
            case 2005:
                return "GB_UNREGISTER_SEND_FAILED (发送注销请求失败)";
            case 2006:
                return "GB_REGISTER_AUTH_INFO_FAILED (添加认证信息失败)";
            case 2007:
                return "GB_REGISTER_EXPIRED (注册已过期)";
            case 2008:
                return "GB_REGISTER_SERVER_UNREACHABLE (注册服务器不可达)";

            // ==========================================
            // 2100-2199: GB28181 媒体流相关错误码
            // ==========================================
            case 2100:
                return "GB_STREAM_START (开始推流)";
            case 2101:
                return "GB_STREAM_STOP (停止推流)";
            case 2102:
                return "GB_STREAM_INVITE_PARSE_FAILED (解析INVITE消息失败)";
            case 2103:
                return "GB_STREAM_SDP_NEGOTIATION_FAILED (SDP协商失败)";
            case 2104:
                return "GB_STREAM_RTP_SETUP_FAILED (RTP通道建立失败)";
            case 2105:
                return "GB_STREAM_ENCODER_INIT_FAILED (编码器初始化失败)";
            case 2106:
                return "GB_STREAM_NETWORK_ERROR (流传输网络错误)";
            case 2107:
                return "GB_STREAM_BYE_RECEIVED (收到BYE请求，停止推流)";
            case 2108:
                return "GB_STREAM_TIMEOUT (推流超时)";
            case 2109:
                return "GB_STREAM_PORT_ALLOC_FAILED (RTP端口分配失败)";

            // ==========================================
            // 2200-2299: GB28181 语音对讲相关错误码
            // ==========================================
            case 2200:
                return "GB_AUDIO_START (开始接收音频)";
            case 2201:
                return "GB_AUDIO_STOP (停止接收音频)";
            case 2202:
                return "GB_AUDIO_INVITE_FAILED (语音对讲INVITE失败)";
            case 2203:
                return "GB_AUDIO_SDP_FAILED (语音对讲SDP协商失败)";
            case 2204:
                return "GB_AUDIO_DECODE_FAILED (音频解码失败)";
            case 2205:
                return "GB_AUDIO_PLAYBACK_FAILED (音频播放失败)";
            case 2206:
                return "GB_AUDIO_NETWORK_ERROR (音频传输网络错误)";

            // ==========================================
            // 2300-2399: GB28181 消息处理错误码
            // ==========================================
            case 2300:
                return "GB_MSG_PARSE_FAILED (XML消息解析失败)";
            case 2301:
                return "GB_MSG_BUILD_FAILED (XML消息构建失败)";
            case 2302:
                return "GB_MSG_SEND_FAILED (消息发送失败)";
            case 2303:
                return "GB_MSG_UNSUPPORTED_CMD (不支持的控制命令)";
            case 2304:
                return "GB_MSG_DEVICEINFO_QUERY_FAILED (设备信息查询响应失败)";
            case 2305:
                return "GB_MSG_CATALOG_QUERY_FAILED (设备目录查询响应失败)";
            case 2306:
                return "GB_MSG_DEVICESTATUS_QUERY_FAILED (设备状态查询响应失败)";
            case 2307:
                return "GB_MSG_PTZ_CONTROL_FAILED (云台控制响应失败)";
            case 2308:
                return "GB_MSG_RECORD_QUERY_FAILED (录像查询响应失败)";
            case 2309:
                return "GB_MSG_HEARTBEAT_FAILED (心跳消息发送失败)";

            // ==========================================
            // 2400-2499: GB28181 设备控制错误码
            // ==========================================
            case 2400:
                return "GB_PTZ_INVALID_PARAM (云台控制参数无效)";
            case 2401:
                return "GB_PTZ_UNSUPPORTED (设备不支持云台控制)";
            case 2402:
                return "GB_RECORD_CONTROL_FAILED (录像控制失败)";
            case 2403:
                return "GB_ALARM_SUBSCRIBE_FAILED (告警订阅失败)";

            // ==========================================
            // 3000-3099: 系统/网络错误码
            // ==========================================
            case 3000:
                return "SYS_INIT_FAILED (系统初始化失败)";
            case 3001:
                return "SYS_CONTEXT_CREATE_FAILED (SIP上下文创建失败)";
            case 3002:
                return "SYS_LISTEN_FAILED (监听端口失败)";
            case 3003:
                return "SYS_THREAD_CREATE_FAILED (线程创建失败)";
            case 3004:
                return "SYS_MUTEX_INIT_FAILED (互斥锁初始化失败)";
            case 3005:
                return "SYS_CONFIG_INVALID (配置参数无效)";

            default:
                return ("Unknown Error (" + std::to_string(code) + ")").data();
        }
    }
};

#endif //GB28181CONSOLE_STATE_CODE_HPP