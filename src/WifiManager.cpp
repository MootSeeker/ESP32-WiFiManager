#include "esp32_wifi_manager/WifiManager.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lwip/inet.h"
}

namespace esp32_wifi_manager {

namespace {

constexpr const char* kTag = "wifi_manager";

bool HasSsid(const WifiCredentials& credentials)
{
    return credentials.ssid[0] != '\0';
}

}  // namespace

WifiManager::~WifiManager()
{
    espIdfAdapter_.DetachEventSink();

    const esp_err_t stopResult = Stop();
    if (stopResult != ESP_OK && stopResult != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Manager teardown stop failed: %s", esp_err_to_name(stopResult));
    }

    const esp_err_t deinitResult = espIdfAdapter_.Deinit();
    if (deinitResult != ESP_OK) {
        ESP_LOGW(kTag, "Manager teardown deinit failed: %s", esp_err_to_name(deinitResult));
    }
}

esp_err_t WifiManager::OnAdapterEvent(const WifiManagerEvent& event, void* eventContext)
{
    auto* manager = static_cast<WifiManager*>(eventContext);
    if (manager == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (manager->externalQueue_ != nullptr) {
        auto queue = static_cast<QueueHandle_t>(manager->externalQueue_);
        if (xQueueSendToBack(queue, &event, 0) != pdTRUE) {
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }

    esp_err_t result = manager->EnqueueEvent(event);
    if (result != ESP_OK) {
        return result;
    }

    return manager->ProcessNextEvent();
}

esp_err_t WifiManager::Init(const WifiManagerConfig& config,
                            WifiStateChangedCallback stateChanged,
                            void* userContext)
{
    config_ = config;
    stateChanged_ = stateChanged;
    userContext_ = userContext;
    initialized_ = true;
    running_ = false;
    forceProvisioning_ = false;
    hasStoredCredentials_ = false;
    activeCredentials_ = {};
    runtimeStatus_ = {};
    eventQueue_.Clear();
    retryScheduler_.Cancel();
    stateMachine_.Reset();

    const esp_err_t result = espIdfAdapter_.Init(config_, &WifiManager::OnAdapterEvent, this);
    if (result != ESP_OK) {
        initialized_ = false;
        return result;
    }

    runtimeStatus_ = {};
    SetState(WifiState::kInit);
    return ESP_OK;
}

esp_err_t WifiManager::Start()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = espIdfAdapter_.Start();
    if (result != ESP_OK) {
        return result;
    }

    running_ = true;

    if (forceProvisioning_) {
        SetState(WifiState::kPortal);
        return ESP_OK;
    }

    WifiCredentialStore credentialStore(config_.nvsNamespace);
    hasStoredCredentials_ = credentialStore.Load(activeCredentials_);
    retryScheduler_.Cancel();

    SetState(stateMachine_.OnStart(forceProvisioning_, hasStoredCredentials_));
    return ESP_OK;
}

esp_err_t WifiManager::EnqueueEvent(const WifiManagerEvent& event)
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    return eventQueue_.Push(event) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t WifiManager::ProcessNextEvent()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    WifiManagerEvent event;
    if (!eventQueue_.Pop(event)) {
        return ESP_ERR_NOT_FOUND;
    }

    return DispatchEvent(event);
}

bool WifiManager::AdvanceRetryTimer(uint32_t elapsedMs)
{
    if (!initialized_) {
        return false;
    }

    if (!retryScheduler_.Advance(elapsedMs)) {
        return false;
    }

    WifiManagerEvent event;
    event.type = WifiManagerEventType::kRetryTimerElapsed;
    if (EnqueueEvent(event) != ESP_OK) {
        return false;
    }

    return ProcessNextEvent() == ESP_OK;
}

esp_err_t WifiManager::DispatchEvent(const WifiManagerEvent& event)
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (event.type) {
    case WifiManagerEventType::kProvisioningRequested:
        ForceProvisioning();
        return ESP_OK;

    case WifiManagerEventType::kCredentialsReceived:
        if (!HasSsid(event.credentials)) {
            return ESP_ERR_INVALID_ARG;
        }

        WifiCredentialStore credentialStore(config_.nvsNamespace);
        if (!credentialStore.Save(event.credentials)) {
            return ESP_FAIL;
        }

        activeCredentials_ = event.credentials;
        hasStoredCredentials_ = true;
        forceProvisioning_ = false;
        runtimeStatus_ = {};
        retryScheduler_.Cancel();

        if (running_) {
            SetState(stateMachine_.OnCredentialsReceived());
        }

        return ESP_OK;

    case WifiManagerEventType::kConnectionSucceeded:
        if (!running_) {
            return ESP_ERR_INVALID_STATE;
        }

        runtimeStatus_ = event.runtimeStatus;
        retryScheduler_.Cancel();
        SetState(stateMachine_.OnConnectionSucceeded());
        return ESP_OK;

    case WifiManagerEventType::kConnectionFailed:
        if (!running_) {
            return ESP_ERR_INVALID_STATE;
        }

        runtimeStatus_ = event.runtimeStatus;
        SetState(stateMachine_.OnConnectionFailed(
            config_.maxConnectAttempts,
            config_.initialReconnectDelayMs,
            config_.maxReconnectDelayMs));

        if (GetState() == WifiState::kWaitingToRetry) {
            if (stateMachine_.GetReconnectDelayMs() == 0) {
                WifiManagerEvent retryEvent;
                retryEvent.type = WifiManagerEventType::kRetryTimerElapsed;
                return DispatchEvent(retryEvent);
            }

            retryScheduler_.Arm(stateMachine_.GetReconnectDelayMs());
        } else {
            retryScheduler_.Cancel();
        }

        return ESP_OK;

    case WifiManagerEventType::kRetryTimerElapsed:
        if (!running_) {
            return ESP_ERR_INVALID_STATE;
        }

        retryScheduler_.Cancel();
        SetState(stateMachine_.OnRetryTimerElapsed());
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t WifiManager::Stop()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    StopPortal();

    const esp_err_t stopResult = espIdfAdapter_.Stop();
    if (stopResult != ESP_OK) {
        running_ = false;
        eventQueue_.Clear();
        retryScheduler_.Cancel();
        runtimeStatus_ = {};
        stateMachine_.Reset();
        SetState(WifiState::kError, false);
        return stopResult;
    }

    running_ = false;
    hasStoredCredentials_ = false;
    eventQueue_.Clear();
    retryScheduler_.Cancel();
    runtimeStatus_ = {};
    SetState(stateMachine_.OnStop(), false);
    return ESP_OK;
}

void WifiManager::ForceProvisioning()
{
    forceProvisioning_ = true;
    retryScheduler_.Cancel();
    if (running_) {
        SetState(stateMachine_.OnProvisioningRequested());
    }
}

WifiState WifiManager::GetState() const
{
    return state_.load();
}

uint32_t WifiManager::GetReconnectDelayMs() const
{
    return stateMachine_.GetReconnectDelayMs();
}

WifiRuntimeStatus WifiManager::GetRuntimeStatus() const
{
    return runtimeStatus_;
}

bool WifiManager::IsRunning() const
{
    return running_;
}

size_t WifiManager::PendingEventCount() const
{
    return eventQueue_.Size();
}

void WifiManager::SetExternalQueue(void* queueHandle)
{
    externalQueue_ = queueHandle;
}

void WifiManager::SetState(WifiState newState)
{
    SetState(newState, true);
}

void WifiManager::SetState(WifiState newState, bool applyRuntime)
{
    const WifiState previousState = state_.load();
    state_.store(newState);

    if (stateChanged_) {
        stateChanged_(newState, userContext_);
    }

    if (applyRuntime) {
        const esp_err_t result = espIdfAdapter_.ApplyState(newState, activeCredentials_, GetReconnectDelayMs());
        if (result != ESP_OK && newState != WifiState::kError) {
            runtimeStatus_ = {};
            state_.store(WifiState::kError);
            if (stateChanged_) {
                stateChanged_(WifiState::kError, userContext_);
            }
            return;
        }
    }

    if (newState == WifiState::kPortal && !portalActive_) {
        StartPortal();
    } else if (previousState == WifiState::kPortal && newState != WifiState::kPortal && portalActive_) {
        StopPortal();
    }
}

esp_err_t WifiManager::OnPortalEvent(const WifiManagerEvent& event, void* eventContext)
{
    auto* manager = static_cast<WifiManager*>(eventContext);
    if (manager == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (manager->externalQueue_ != nullptr) {
        auto queue = static_cast<QueueHandle_t>(manager->externalQueue_);
        if (xQueueSendToBack(queue, &event, 0) != pdTRUE) {
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }

    esp_err_t result = manager->EnqueueEvent(event);
    if (result != ESP_OK) {
        return result;
    }

    return manager->ProcessNextEvent();
}

esp_err_t WifiManager::StartPortal()
{
    if (portalActive_) {
        return ESP_OK;
    }

    esp_netif_ip_info_t ipInfo{};
    esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (apNetif != nullptr) {
        esp_netif_get_ip_info(apNetif, &ipInfo);
    }

    const uint32_t apIp = ipInfo.ip.addr != 0 ? ipInfo.ip.addr : htonl(0xC0A80401);

    esp_err_t result = portalDns_.Start(apIp);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Failed to start DNS hijack: %s", esp_err_to_name(result));
    }

    result = portalHttp_.Start(&WifiManager::OnPortalEvent, this, scanService_, config_.portalPort);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start portal HTTP: %s", esp_err_to_name(result));
        portalDns_.Stop();
        return result;
    }

    portalActive_ = true;
    ESP_LOGI(kTag, "Captive portal started");
    return ESP_OK;
}

esp_err_t WifiManager::StopPortal()
{
    if (!portalActive_) {
        return ESP_OK;
    }

    portalHttp_.Stop();
    portalDns_.Stop();
    portalActive_ = false;
    ESP_LOGI(kTag, "Captive portal stopped");
    return ESP_OK;
}

}  // namespace esp32_wifi_manager