//
// Created by pengx on 2026/3/10.
//

#ifndef GB28181CONSOLE_REGISTER_MANAGER_HPP
#define GB28181CONSOLE_REGISTER_MANAGER_HPP

#include <atomic>
#include <functional>
#include <mutex>

#include "logger.hpp"
#include "sip.hpp"
#include "sip_context.hpp"

#define REGISTER_EXPIRED_TIME 7200 // 注册有效期

class RegisterManager {
public:
    using StateCallback = std::function<void(int error_code)>;

    explicit RegisterManager(SipContext* context);

    void setStateCallback(StateCallback callback);

    // 注册
    void startRegistration();

    // 处理401/407认证
    void handleAuthentication();

    // 注销（expires=0）
    void stopRegistration();

    bool isRegistered() const {
        return _state == Sip::RegisterState::SUCCESS;
    }

    void setState(Sip::RegisterState new_state);

    Sip::RegisterState getState() const {
        return _state;
    }

    std::string toStateString(Sip::RegisterState state);

    int getRegisterId() const {
        return _register_id;
    }

private:
    Logger _logger;
    SipContext* _sip_context_ptr;

    std::atomic<int> _register_id{-1};
    std::atomic<Sip::RegisterState> _state{Sip::RegisterState::IDLE};
    std::atomic<bool> _is_registering{false};
    std::mutex _mutex;

    StateCallback _state_callback;

    void send_register_request(uint16_t expires);

    void exchange_state(Sip::RegisterState new_state, int error_code = 0);
};

#endif //GB28181CONSOLE_REGISTER_MANAGER_HPP