#pragma once

#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiManagerStateMachine {
public:
    void Reset();

    WifiState OnStart(bool forceProvisioning, bool hasStoredCredentials);
    WifiState OnProvisioningRequested();
    WifiState OnCredentialsReceived();
    WifiState OnConnectionSucceeded();
    WifiState OnConnectionFailed(uint8_t maxConnectAttempts,
                                 uint32_t initialReconnectDelayMs,
                                 uint32_t maxReconnectDelayMs);
    WifiState OnStop();

    WifiState GetState() const;
    uint8_t GetConnectAttempts() const;
    uint32_t GetReconnectDelayMs() const;

private:
    uint32_t CalculateReconnectDelayMs(uint32_t initialReconnectDelayMs,
                                       uint32_t maxReconnectDelayMs) const;

    WifiState state_ = WifiState::kInit;
    uint8_t connectAttempts_ = 0;
    uint32_t reconnectDelayMs_ = 0;
};

}  // namespace esp32_wifi_manager