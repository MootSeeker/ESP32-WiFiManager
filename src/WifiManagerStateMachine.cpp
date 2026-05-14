#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"

namespace esp32_wifi_manager {

void WifiManagerStateMachine::Reset()
{
    state_ = WifiState::kInit;
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
}

WifiState WifiManagerStateMachine::OnStart(bool forceProvisioning, bool hasStoredCredentials)
{
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
    state_ = forceProvisioning || !hasStoredCredentials ? WifiState::kPortal : WifiState::kConnecting;
    return state_;
}

WifiState WifiManagerStateMachine::OnProvisioningRequested()
{
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
    state_ = WifiState::kPortal;
    return state_;
}

WifiState WifiManagerStateMachine::OnCredentialsReceived()
{
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
    state_ = WifiState::kConnecting;
    return state_;
}

WifiState WifiManagerStateMachine::OnConnectionSucceeded()
{
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
    state_ = WifiState::kConnected;
    return state_;
}

WifiState WifiManagerStateMachine::OnConnectionFailed(uint8_t maxConnectAttempts,
                                                      uint32_t initialReconnectDelayMs,
                                                      uint32_t maxReconnectDelayMs)
{
    if (maxConnectAttempts == 0) {
        reconnectDelayMs_ = 0;
        state_ = WifiState::kPortal;
        return state_;
    }

    ++connectAttempts_;
    state_ = connectAttempts_ < maxConnectAttempts ? WifiState::kWaitingToRetry : WifiState::kPortal;
    reconnectDelayMs_ = state_ == WifiState::kWaitingToRetry
                            ? CalculateReconnectDelayMs(initialReconnectDelayMs, maxReconnectDelayMs)
                            : 0;
    return state_;
}

WifiState WifiManagerStateMachine::OnRetryTimerElapsed()
{
    state_ = WifiState::kConnecting;
    return state_;
}

WifiState WifiManagerStateMachine::OnStop()
{
    connectAttempts_ = 0;
    reconnectDelayMs_ = 0;
    state_ = WifiState::kStopped;
    return state_;
}

WifiState WifiManagerStateMachine::GetState() const
{
    return state_;
}

uint8_t WifiManagerStateMachine::GetConnectAttempts() const
{
    return connectAttempts_;
}

uint32_t WifiManagerStateMachine::GetReconnectDelayMs() const
{
    return reconnectDelayMs_;
}

uint32_t WifiManagerStateMachine::CalculateReconnectDelayMs(uint32_t initialReconnectDelayMs,
                                                            uint32_t maxReconnectDelayMs) const
{
    if (initialReconnectDelayMs == 0 || maxReconnectDelayMs == 0) {
        return 0;
    }

    uint64_t delayMs = initialReconnectDelayMs;
    for (uint8_t attempt = 1; attempt < connectAttempts_; ++attempt) {
        delayMs *= 2;
        if (delayMs >= maxReconnectDelayMs) {
            return maxReconnectDelayMs;
        }
    }

    return delayMs > maxReconnectDelayMs ? maxReconnectDelayMs : static_cast<uint32_t>(delayMs);
}

}  // namespace esp32_wifi_manager