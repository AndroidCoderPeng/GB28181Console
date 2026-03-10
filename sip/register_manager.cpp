//
// Created by pengx on 2026/3/10.
//

#include "register_manager.hpp"

#include <eXosip2/eX_setup.h>

#include "state_code.hpp"

RegisterManager::RegisterManager(SipContext* context) : _logger("RegisterManager"), _sip_context_ptr(context) {
    _logger.i("RegisterManager created");
}

void RegisterManager::setStateCallback(StateCallback callback) {
    _state_callback = std::move(callback);
}

void RegisterManager::startRegistration() {
    if (_is_registering.load()) {
        _logger.w("注册正在进行中");
        return;
    }

    _is_registering = true;

    send_register_request(REGISTER_EXPIRED_TIME);
}

void RegisterManager::stopRegistration() {
    if (_register_id <= 0) {
        _logger.w("未注册，无需注销");
        return;
    }

    _is_registering = false;

    send_register_request(0);
}

void RegisterManager::send_register_request(uint16_t expires) {
    if (!_sip_context_ptr->isValid()) {
        _logger.e("SIP 上下文无效");
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    const bool is_register = (expires != 0);
    _logger.iFmt("%s中...", is_register ? "注册" : "注销");

    _sip_context_ptr->lock();

    osip_message_t* reg_request = nullptr;
    if (is_register) {
        const int rid = eXosip_register_build_initial_register(_sip_context_ptr->getContextPtr(),
                                                               _sip_context_ptr->getFromUri().c_str(),
                                                               _sip_context_ptr->getProxyUri().c_str(),
                                                               nullptr,
                                                               expires,
                                                               &reg_request);
        if (rid < 0) {
            _sip_context_ptr->unlock();
            return;
        }
        _register_id = rid;
    } else {
        _logger.dFmt("使用默认 reg 执行注销，当前 register id: %d", _register_id.load());
    }

    /**
     * 发送注册或者注销消息
     *
     * 当 reg_request 为 nullptr 时，eXosip2 会自动使用默认的 REGISTER 请求
     * 当 reg 不为 nullptr 时：使用你手动构建的请求
     * */
    const int result = eXosip_register_send_register(_sip_context_ptr->getContextPtr(),
                                                     _register_id.load(),
                                                     reg_request);
    _sip_context_ptr->unlock();

    if (result != OSIP_SUCCESS) {
        _logger.eFmt("发送%s消息失败: %d", is_register ? "注册" : "注销", result);
        if (is_register) {
            _register_id = -1;
            // 这里不调用 setState，让调用者处理失败状态
        }
    } else if (is_register) {
        // 初始注册请求发送成功，设置状态为 SENT_INITIAL
        exchange_state(Sip::RegisterState::SENT_INITIAL);
    }
    _logger.iFmt("%s请求已发送，等待服务器响应...", is_register ? "注册" : "注销");
}

void RegisterManager::handleAuthentication() {
    _logger.i("开始注册认证流程");

    if (_register_id <= 0) {
        exchange_state(Sip::RegisterState::FAILED, 2007);
        return;
    }

    _sip_context_ptr->lock();

    // 添加认证信息（如果已存在也没关系，会更新）
    const auto parameter = _sip_context_ptr->getSipParameter();
    eXosip_add_authentication_info(_sip_context_ptr->getContextPtr(),
                                   parameter.deviceName.c_str(),
                                   parameter.deviceCode.c_str(),
                                   parameter.password.c_str(),
                                   "MD5",
                                   nullptr);
    _logger.i("认证信息已添加");

    // 构建认证请求
    osip_message_t* auth_reg = nullptr;
    eXosip_register_build_register(_sip_context_ptr->getContextPtr(),
                                   _register_id.load(),
                                   REGISTER_EXPIRED_TIME,
                                   &auth_reg);
    _logger.i("认证注册消息已构建");

    const int result = eXosip_register_send_register(_sip_context_ptr->getContextPtr(),
                                                     _register_id.load(),
                                                     auth_reg);
    _sip_context_ptr->unlock();

    if (result != OSIP_SUCCESS) {
        _logger.eFmt("发送认证注册消息失败: %d", result);
        exchange_state(Sip::RegisterState::FAILED, 2003); // 设置失败状态
        return;
    }
    exchange_state(Sip::RegisterState::SENT_AUTH);
    _logger.i("注册认证请求已发送");
}

void RegisterManager::setState(Sip::RegisterState new_state) {
    exchange_state(new_state);
}

std::string RegisterManager::toStateString(Sip::RegisterState state) { // NOLINT
    switch (state) {
        case Sip::RegisterState::IDLE:
            return "idle";
        case Sip::RegisterState::SENT_INITIAL:
            return "sent_initial";
        case Sip::RegisterState::SENT_AUTH:
            return "sent_auth";
        case Sip::RegisterState::SUCCESS:
            return "success";
        case Sip::RegisterState::FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

void RegisterManager::exchange_state(Sip::RegisterState new_state, const int error_code) {
    Sip::RegisterState old_state = _state.exchange(new_state);
    _logger.iFmt("状态变更: %d -> %d", static_cast<int>(old_state), static_cast<int>(new_state));

    // 调用外部回调
    if (_state_callback && error_code != 0) {
        _state_callback(error_code);
    }
}