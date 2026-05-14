#pragma once

#include <atomic>
#include <cstddef>

#include "esp_err.h"

#include "esp32_wifi_manager/CaptivePortalDns.hpp"
#include "esp32_wifi_manager/CaptivePortalHttp.hpp"
#include "esp32_wifi_manager/WifiCredentialStore.hpp"
#include "esp32_wifi_manager/WifiManagerEspIdfAdapter.hpp"
#include "esp32_wifi_manager/WifiManagerEventQueue.hpp"
#include "esp32_wifi_manager/WifiRetryScheduler.hpp"
#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"
#include "esp32_wifi_manager/WifiManagerTypes.hpp"
#include "esp32_wifi_manager/WifiScanService.hpp"

namespace esp32_wifi_manager {

class WifiManager {
public:
    WifiManager() = default;
    ~WifiManager();

    esp_err_t Init(const WifiManagerConfig& config,
                   WifiStateChangedCallback stateChanged = nullptr,
                   void* userContext = nullptr);

    esp_err_t Start();
    esp_err_t EnqueueEvent(const WifiManagerEvent& event);
    esp_err_t ProcessNextEvent();
    bool AdvanceRetryTimer(uint32_t elapsedMs);
    esp_err_t DispatchEvent(const WifiManagerEvent& event);
    esp_err_t Stop();
    void ForceProvisioning();

    WifiState GetState() const;
    uint32_t GetReconnectDelayMs() const;
    WifiRuntimeStatus GetRuntimeStatus() const;
    bool IsRunning() const;
    size_t PendingEventCount() const;

    void SetExternalQueue(void* queueHandle);

private:
    static esp_err_t OnAdapterEvent(const WifiManagerEvent& event, void* eventContext);
    static esp_err_t OnPortalEvent(const WifiManagerEvent& event, void* eventContext);
    esp_err_t StartPortal();
    esp_err_t StopPortal();
    void SetState(WifiState newState);
    void SetState(WifiState newState, bool applyRuntime);

    WifiManagerConfig config_{};
    WifiStateChangedCallback stateChanged_ = nullptr;
    void* userContext_ = nullptr;
    std::atomic<WifiState> state_{WifiState::kInit};
    WifiCredentials activeCredentials_{};
    WifiRuntimeStatus runtimeStatus_{};
    WifiManagerEspIdfAdapter espIdfAdapter_{};
    WifiManagerEventQueue eventQueue_{};
    WifiRetryScheduler retryScheduler_{};
    WifiManagerStateMachine stateMachine_{};
    CaptivePortalDns portalDns_{};
    CaptivePortalHttp portalHttp_{};
    WifiScanService scanService_{};
    bool initialized_ = false;
    bool running_ = false;
    bool forceProvisioning_ = false;
    bool hasStoredCredentials_ = false;
    bool portalActive_ = false;
    void* externalQueue_ = nullptr;
};

}  // namespace esp32_wifi_manager