#pragma once

#include <atomic>

#include "esp_err.h"

#include "esp32_wifi_manager/WifiCredentialStore.hpp"
#include "esp32_wifi_manager/WifiManagerEventQueue.hpp"
#include "esp32_wifi_manager/WifiRetryScheduler.hpp"
#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"
#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiManager {
public:
    WifiManager() = default;

    esp_err_t Init(const WifiManagerConfig& config,
                   WifiStateChangedCallback stateChanged = nullptr,
                   void* userContext = nullptr);

    esp_err_t Start();
    esp_err_t EnqueueEvent(const WifiManagerEvent& event);
    esp_err_t ProcessNextEvent();
    bool AdvanceRetryTimer(uint32_t elapsedMs);
    esp_err_t DispatchEvent(const WifiManagerEvent& event);
    void Stop();
    void ForceProvisioning();

    WifiState GetState() const;
    uint32_t GetReconnectDelayMs() const;
    bool IsRunning() const;
    size_t PendingEventCount() const;

private:
    void SetState(WifiState newState);

    WifiManagerConfig config_{};
    WifiStateChangedCallback stateChanged_ = nullptr;
    void* userContext_ = nullptr;
    std::atomic<WifiState> state_{WifiState::kInit};
    WifiCredentials activeCredentials_{};
    WifiManagerEventQueue eventQueue_{};
    WifiRetryScheduler retryScheduler_{};
    WifiManagerStateMachine stateMachine_{};
    bool initialized_ = false;
    bool running_ = false;
    bool forceProvisioning_ = false;
    bool hasStoredCredentials_ = false;
};

}  // namespace esp32_wifi_manager