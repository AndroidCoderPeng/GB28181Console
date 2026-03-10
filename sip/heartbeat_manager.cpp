//
// Created by pengx on 2026/3/10.
//

#include "heartbeat_manager.hpp"

HeartbeatManager::HeartbeatManager(const int interval) : _logger("HeartbeatManager"), _interval_seconds(interval) {
    _logger.i("HeartbeatManager created");
}

HeartbeatManager::~HeartbeatManager() {
    stop();
    _logger.i("HeartbeatManager deconstruction");
}

void HeartbeatManager::setCallback(HeartbeatSendCallback callback) {
    std::lock_guard<std::mutex> lock(_mutex);
    _callback = std::move(callback);
}

bool HeartbeatManager::start() {
    std::lock_guard<std::mutex> lock(_mutex);

    // 防止重复启动
    if (_is_running.load()) {
        _logger.w("心跳线程已在运行");
        return false;
    }

    if (_thread_ptr && _thread_ptr->joinable()) {
        _logger.w("心跳线程对象已存在");
        return false;
    }

    if (!_callback) {
        _logger.e("心跳发送回调未设置");
        return false;
    }

    _logger.i("启动心跳线程");

    try {
        _is_running = true;
        _thread_ptr = std::make_unique<std::thread>(&HeartbeatManager::heartbeat_loop, this);
        _logger.dBox()
               .add("心跳线程已启动")
               .addFmt("心跳间隔: %d 秒", _interval_seconds)
               .print();
        return true;
    } catch (const std::exception& e) {
        _logger.eFmt("启动心跳线程失败: %s", e.what());
        _is_running = false;
        return false;
    }
}

void HeartbeatManager::stop() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_is_running.load()) {
        return;
    }

    _logger.i("正在停止心跳线程...");

    _is_running = false;

    if (_thread_ptr && _thread_ptr->joinable()) {
        _thread_ptr->join();
    }
    _thread_ptr.reset();

    _logger.i("心跳线程已停止");
}

bool HeartbeatManager::isRunning() const {
    return _is_running.load();
}

void HeartbeatManager::heartbeat_loop() {
    int heartbeat_count = 0;
    while (_is_running.load()) {
        // ============ 分段睡眠，便于快速响应停止信号 ============
        for (int i = 0; i < _interval_seconds * 10 && _is_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // ============ 睡眠结束后再次检查停止标志 ============
        if (!_is_running.load()) {
            _logger.i("检测到停止信号，退出心跳循环");
            break;
        }

        // ============ 发送心跳 ============
        heartbeat_count++;
        _logger.dFmt("发送第 %d 次心跳", heartbeat_count);
        if (_callback) {
            _callback();
        }
    }
    _logger.dBox()
           .add("心跳线程正常退出")
           .addFmt("共发送心跳: %d 次", heartbeat_count)
           .print();
}