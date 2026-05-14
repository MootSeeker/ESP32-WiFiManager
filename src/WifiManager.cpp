#include "esp32_wifi_manager/WifiManager.hpp"

namespace esp32_wifi_manager {

namespace {

bool HasSsid(const WifiCredentials& credentials)
{
    return credentials.ssid[0] != '\0';
}

}  // namespace

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
    eventQueue_.Clear();
    retryScheduler_.Cancel();
    stateMachine_.Reset();
    SetState(WifiState::kInit);
    return ESP_OK;
}

esp_err_t WifiManager::Start()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
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
    return EnqueueEvent(event) == ESP_OK;
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
        retryScheduler_.Cancel();

        if (running_) {
            SetState(stateMachine_.OnCredentialsReceived());
        }

        return ESP_OK;

    case WifiManagerEventType::kConnectionSucceeded:
        if (!running_) {
            return ESP_ERR_INVALID_STATE;
        }

        retryScheduler_.Cancel();
        SetState(stateMachine_.OnConnectionSucceeded());
        return ESP_OK;

    case WifiManagerEventType::kConnectionFailed:
        if (!running_) {
            return ESP_ERR_INVALID_STATE;
        }

        SetState(stateMachine_.OnConnectionFailed(
            config_.maxConnectAttempts,
            config_.initialReconnectDelayMs,
            config_.maxReconnectDelayMs));

        if (GetState() == WifiState::kWaitingToRetry) {
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

void WifiManager::Stop()
{
    if (!initialized_) {
        return;
    }

    running_ = false;
    hasStoredCredentials_ = false;
    eventQueue_.Clear();
    retryScheduler_.Cancel();
    SetState(stateMachine_.OnStop());
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

bool WifiManager::IsRunning() const
{
    return running_;
}

size_t WifiManager::PendingEventCount() const
{
    return eventQueue_.Size();
}

void WifiManager::SetState(WifiState newState)
{
    state_.store(newState);
    if (stateChanged_) {
        stateChanged_(newState, userContext_);
    }
}

}  // namespace esp32_wifi_manager