#pragma once

#include <cstdint>

namespace esp32_wifi_manager {

enum class WifiState : uint8_t {
    kInit = 0,
    kConnecting,
    kConnected,
    kPortal,
    kStopped,
    kError,
};

enum class WifiManagerEventType : uint8_t {
    kProvisioningRequested = 0,
    kCredentialsReceived,
    kConnectionSucceeded,
    kConnectionFailed,
};

struct WifiCredentials {
    char ssid[33];
    char password[65];
};

struct WifiManagerEvent {
    WifiManagerEventType type = WifiManagerEventType::kProvisioningRequested;
    WifiCredentials credentials{};
};

struct WifiManagerConfig {
    const char* nvsNamespace = "wifi_mgr";
    const char* apSsidPrefix = "ESP32-WiFiMgr-";
    uint16_t portalPort = 80;
    uint8_t apMaxConnections = 4;
    uint8_t maxConnectAttempts = 5;
    uint32_t connectTimeoutMs = 15000;
};

using WifiStateChangedCallback = void (*)(WifiState newState, void* userContext);

}  // namespace esp32_wifi_manager