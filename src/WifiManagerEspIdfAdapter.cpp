#include "esp32_wifi_manager/WifiManagerEspIdfAdapter.hpp"

#include <cstdio>

extern "C" {
#include "esp_log.h"
}

namespace {

constexpr const char* kTag = "wifi_adapter";

bool HasSsid(const esp32_wifi_manager::WifiCredentials& credentials)
{
    return credentials.ssid[0] != '\0';
}

}  // namespace

namespace esp32_wifi_manager {

esp_err_t WifiManagerEspIdfAdapter::Init(const WifiManagerConfig& config,
                                         WifiManagerEventSink eventSink,
                                         void* eventContext)
{
    if (initialized_) {
        return ESP_OK;
    }

    config_ = config;
    eventSink_ = eventSink;
    eventContext_ = eventContext;

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    staNetif_ = esp_netif_create_default_wifi_sta();
    if (staNetif_ == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifiInitConfig);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    result = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WifiManagerEspIdfAdapter::EventHandler,
        this,
        &wifiEventHandler_);
    if (result != ESP_OK) {
        return result;
    }

    result = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WifiManagerEspIdfAdapter::EventHandler,
        this,
        &ipEventHandler_);
    if (result != ESP_OK) {
        return result;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t WifiManagerEspIdfAdapter::Start()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    started_ = true;
    return EnsureWifiStarted();
}

esp_err_t WifiManagerEspIdfAdapter::ApplyState(WifiState state,
                                               const WifiCredentials& credentials,
                                               uint32_t reconnectDelayMs)
{
    scheduledReconnectDelayMs_ = reconnectDelayMs;

    if (!initialized_ || !started_) {
        return ESP_OK;
    }

    switch (state) {
    case WifiState::kConnecting:
        return ConnectStation(credentials);

    case WifiState::kWaitingToRetry:
    case WifiState::kConnected:
        return ESP_OK;

    case WifiState::kPortal:
    case WifiState::kStopped:
        scheduledReconnectDelayMs_ = 0;
        return wifiStarted_ ? esp_wifi_disconnect() : ESP_OK;

    case WifiState::kInit:
    case WifiState::kError:
        return ESP_OK;
    }

    return ESP_OK;
}

void WifiManagerEspIdfAdapter::Stop()
{
    scheduledReconnectDelayMs_ = 0;
    started_ = false;

    if (wifiStarted_) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifiStarted_ = false;
    }
}

bool WifiManagerEspIdfAdapter::IsInitialized() const
{
    return initialized_;
}

void WifiManagerEspIdfAdapter::EventHandler(void* arg,
                                            esp_event_base_t eventBase,
                                            int32_t eventId,
                                            void* eventData)
{
    auto* adapter = static_cast<WifiManagerEspIdfAdapter*>(arg);
    if (adapter == nullptr) {
        return;
    }

    WifiManagerEvent event;

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        event.type = WifiManagerEventType::kConnectionFailed;
        if (eventData != nullptr) {
            const auto* disconnected = static_cast<const wifi_event_sta_disconnected_t*>(eventData);
            event.runtimeStatus.disconnectReason = static_cast<uint8_t>(disconnected->reason);
        }

        adapter->EmitEvent(event);
        return;
    }

    if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        event.type = WifiManagerEventType::kConnectionSucceeded;
        if (eventData != nullptr) {
            const auto* gotIp = static_cast<const ip_event_got_ip_t*>(eventData);
            event.runtimeStatus.ipAddress = gotIp->ip_info.ip.addr;
            event.runtimeStatus.netmask = gotIp->ip_info.netmask.addr;
            event.runtimeStatus.gateway = gotIp->ip_info.gw.addr;
        }

        adapter->EmitEvent(event);
    }
}

esp_err_t WifiManagerEspIdfAdapter::EmitEvent(const WifiManagerEvent& event)
{
    if (eventSink_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return eventSink_(event, eventContext_);
}

esp_err_t WifiManagerEspIdfAdapter::EnsureWifiStarted()
{
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        return result;
    }

    if (wifiStarted_) {
        return ESP_OK;
    }

    result = esp_wifi_start();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    wifiStarted_ = true;
    return ESP_OK;
}

esp_err_t WifiManagerEspIdfAdapter::ConnectStation(const WifiCredentials& credentials)
{
    if (!HasSsid(credentials)) {
        ESP_LOGW(kTag, "Skipping station connect without credentials");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = EnsureWifiStarted();
    if (result != ESP_OK) {
        return result;
    }

    wifi_config_t wifiConfig = {};
    std::snprintf(reinterpret_cast<char*>(wifiConfig.sta.ssid),
                  sizeof(wifiConfig.sta.ssid),
                  "%s",
                  credentials.ssid);
    std::snprintf(reinterpret_cast<char*>(wifiConfig.sta.password),
                  sizeof(wifiConfig.sta.password),
                  "%s",
                  credentials.password);
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    result = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (result != ESP_OK) {
        return result;
    }

    return esp_wifi_connect();
}

}  // namespace esp32_wifi_manager