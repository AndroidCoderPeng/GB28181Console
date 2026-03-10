//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_HEARTBEAT_MANAGER_HPP
#define GB28181CONSOLE_HEARTBEAT_MANAGER_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "logger.hpp"

class HeartbeatManager {
public:
    /**
     * @param interval 心跳间隔（秒）
     */
    explicit HeartbeatManager(int interval = 60);

    ~HeartbeatManager();

    /**
     * 心跳发送回调函数类型
     * @return true=发送成功, false=发送失败
     */
    using HeartbeatSendCallback = std::function<void()>;

    /**
     * 设置心跳发送回调
     * 必须在 start() 之前调用
     */
    void setCallback(HeartbeatSendCallback callback);

    /**
     * 启动心跳线程
     * @return true=启动成功, false=启动失败（可能已运行）
     */
    bool start();

    /**
     * 停止心跳线程
     * 会等待线程完全退出
     */
    void stop();

    /**
     * 检查心跳线程是否正在运行
     */
    bool isRunning() const;

private:
    Logger _logger;
    const int _interval_seconds;

    // 线程控制
    std::atomic<bool> _is_running{false};
    std::unique_ptr<std::thread> _thread_ptr;
    std::mutex _mutex;

    // 心跳发送回调
    HeartbeatSendCallback _callback;

    /**
     * 心跳线程主函数
     */
    void heartbeat_loop();
};

#endif //GB28181CONSOLE_HEARTBEAT_MANAGER_HPP