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
    WifiState OnConnectionFailed(uint8_t maxConnectAttempts);
    WifiState OnStop();

    WifiState GetState() const;
    uint8_t GetConnectAttempts() const;

private:
    WifiState state_ = WifiState::kInit;
    uint8_t connectAttempts_ = 0;
};

}  // namespace esp32_wifi_manager