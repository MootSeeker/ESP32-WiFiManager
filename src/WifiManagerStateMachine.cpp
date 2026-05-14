#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"

namespace esp32_wifi_manager {

void WifiManagerStateMachine::Reset()
{
    state_ = WifiState::kInit;
    connectAttempts_ = 0;
}

WifiState WifiManagerStateMachine::OnStart(bool forceProvisioning, bool hasStoredCredentials)
{
    connectAttempts_ = 0;
    state_ = forceProvisioning || !hasStoredCredentials ? WifiState::kPortal : WifiState::kConnecting;
    return state_;
}

WifiState WifiManagerStateMachine::OnProvisioningRequested()
{
    connectAttempts_ = 0;
    state_ = WifiState::kPortal;
    return state_;
}

WifiState WifiManagerStateMachine::OnCredentialsReceived()
{
    connectAttempts_ = 0;
    state_ = WifiState::kConnecting;
    return state_;
}

WifiState WifiManagerStateMachine::OnConnectionSucceeded()
{
    connectAttempts_ = 0;
    state_ = WifiState::kConnected;
    return state_;
}

WifiState WifiManagerStateMachine::OnConnectionFailed(uint8_t maxConnectAttempts)
{
    if (maxConnectAttempts == 0) {
        state_ = WifiState::kPortal;
        return state_;
    }

    ++connectAttempts_;
    state_ = connectAttempts_ < maxConnectAttempts ? WifiState::kConnecting : WifiState::kPortal;
    return state_;
}

WifiState WifiManagerStateMachine::OnStop()
{
    connectAttempts_ = 0;
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

}  // namespace esp32_wifi_manager