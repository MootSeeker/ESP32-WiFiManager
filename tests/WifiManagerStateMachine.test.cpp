#include <cstdlib>
#include <iostream>

#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"

using esp32_wifi_manager::WifiManagerStateMachine;
using esp32_wifi_manager::WifiState;

namespace {

bool ExpectEqual(WifiState actual, WifiState expected, const char* message)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << message << std::endl;
    return false;
}

bool ExpectEqual(uint8_t actual, uint8_t expected, const char* message)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << message << std::endl;
    return false;
}

bool ShouldRetryBeforePortalFallback()
{
    WifiManagerStateMachine stateMachine;

    if (!ExpectEqual(stateMachine.OnStart(false, true), WifiState::kConnecting,
                     "expected stored credentials to start in connecting state")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3), WifiState::kConnecting,
                     "expected first connection failure to stay in connecting state")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 1,
                     "expected first connection failure to increment retry count")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3), WifiState::kConnecting,
                     "expected second connection failure to still retry")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3), WifiState::kPortal,
                     "expected final allowed connection failure to fall back to portal")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetConnectAttempts(), 3,
                       "expected retry count to reflect the failure threshold");
}

bool ShouldResetRetriesAfterSuccessAndProvisioning()
{
    WifiManagerStateMachine stateMachine;

    stateMachine.OnStart(false, true);
    stateMachine.OnConnectionFailed(4);
    stateMachine.OnConnectionFailed(4);

    if (!ExpectEqual(stateMachine.OnConnectionSucceeded(), WifiState::kConnected,
                     "expected success event to transition to connected")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 0,
                     "expected success to clear retry count")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnProvisioningRequested(), WifiState::kPortal,
                     "expected provisioning request to enter portal mode")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetConnectAttempts(), 0,
                       "expected provisioning request to keep retry count cleared");
}

}  // namespace

int main()
{
    if (!ShouldRetryBeforePortalFallback()) {
        return EXIT_FAILURE;
    }

    if (!ShouldResetRetriesAfterSuccessAndProvisioning()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}