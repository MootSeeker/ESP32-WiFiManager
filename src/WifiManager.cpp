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

    SetState(hasStoredCredentials_ ? WifiState::kConnecting : WifiState::kPortal);
    return ESP_OK;
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

        if (running_) {
            SetState(WifiState::kConnecting);
        }

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
    SetState(WifiState::kStopped);
}

void WifiManager::ForceProvisioning()
{
    forceProvisioning_ = true;
    if (running_) {
        SetState(WifiState::kPortal);
    }
}

WifiState WifiManager::GetState() const
{
    return state_.load();
}

bool WifiManager::IsRunning() const
{
    return running_;
}

void WifiManager::SetState(WifiState newState)
{
    state_.store(newState);
    if (stateChanged_) {
        stateChanged_(newState, userContext_);
    }
}

}  // namespace esp32_wifi_manager