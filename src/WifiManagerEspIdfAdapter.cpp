#include "esp32_wifi_manager/WifiManagerEspIdfAdapter.hpp"

#include <cstdio>
#include <cstring>

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
        config_ = config;
        eventSink_ = eventSink;
        eventContext_ = eventContext;
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

    if (staNetif_ == nullptr) {
        staNetif_ = esp_netif_create_default_wifi_sta();
        if (staNetif_ == nullptr) {
            return ESP_FAIL;
        }

        ownsStaNetif_ = true;
    }

    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifiInitConfig);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        Deinit();
        return result;
    }

    ownsWifiInit_ = ownsWifiInit_ || result == ESP_OK;

    result = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WifiManagerEspIdfAdapter::EventHandler,
        this,
        &wifiEventHandler_);
    if (result != ESP_OK) {
        Deinit();
        return result;
    }

    wifiHandlerRegistered_ = true;

    result = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WifiManagerEspIdfAdapter::EventHandler,
        this,
        &ipEventHandler_);
    if (result != ESP_OK) {
        Deinit();
        return result;
    }

    ipHandlerRegistered_ = true;

    initialized_ = true;
    return ESP_OK;
}

esp_err_t WifiManagerEspIdfAdapter::Start()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    suppressDisconnectEvent_ = false;
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
        scheduledReconnectDelayMs_ = 0;
        if (!wifiStarted_) {
            return ESP_OK;
        }

        {
            const esp_err_t disconnectResult = esp_wifi_disconnect();
            suppressDisconnectEvent_ = disconnectResult == ESP_OK;
            if (disconnectResult == ESP_ERR_WIFI_NOT_CONNECT) {
                suppressDisconnectEvent_ = false;
                return ESP_OK;
            }

            return disconnectResult;
        }

    case WifiState::kStopped:
        scheduledReconnectDelayMs_ = 0;
        if (!wifiStarted_) {
            return ESP_OK;
        }

        {
            const esp_err_t disconnectResult = esp_wifi_disconnect();
            suppressDisconnectEvent_ = disconnectResult == ESP_OK;
            if (disconnectResult != ESP_OK && disconnectResult != ESP_ERR_WIFI_NOT_CONNECT) {
                return disconnectResult;
            }

            const esp_err_t stopResult = esp_wifi_stop();
            if (stopResult != ESP_OK && stopResult != ESP_ERR_INVALID_STATE) {
                return stopResult;
            }

            wifiStarted_ = false;
            return ESP_OK;
        }

    case WifiState::kInit:
    case WifiState::kError:
        return ESP_OK;
    }

    return ESP_OK;
}

void WifiManagerEspIdfAdapter::DetachEventSink()
{
    eventSink_ = nullptr;
    eventContext_ = nullptr;
}

esp_err_t WifiManagerEspIdfAdapter::Stop()
{
    scheduledReconnectDelayMs_ = 0;
    started_ = false;

    if (!wifiStarted_) {
        return ESP_OK;
    }

    suppressDisconnectEvent_ = true;

    const esp_err_t disconnectResult = esp_wifi_disconnect();
    if (disconnectResult == ESP_ERR_WIFI_NOT_CONNECT) {
        suppressDisconnectEvent_ = false;
    } else if (disconnectResult != ESP_OK) {
        return disconnectResult;
    }

    const esp_err_t stopResult = esp_wifi_stop();
    if (stopResult == ESP_OK || stopResult == ESP_ERR_INVALID_STATE) {
        wifiStarted_ = false;
        return ESP_OK;
    }

    return stopResult;
}

esp_err_t WifiManagerEspIdfAdapter::Deinit()
{
    const esp_err_t stopResult = Stop();

    if (wifiHandlerRegistered_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler_);
        wifiHandlerRegistered_ = false;
        wifiEventHandler_ = nullptr;
    }

    if (ipHandlerRegistered_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ipEventHandler_);
        ipHandlerRegistered_ = false;
        ipEventHandler_ = nullptr;
    }

    if (stopResult != ESP_OK) {
        ESP_LOGW(kTag, "Skipping Wi-Fi driver deinit because stop did not complete cleanly");
        eventSink_ = nullptr;
        eventContext_ = nullptr;
        scheduledReconnectDelayMs_ = 0;
        suppressDisconnectEvent_ = false;
        initialized_ = false;
        started_ = false;
        return stopResult;
    }

    if (ownsWifiInit_) {
        esp_wifi_deinit();
        ownsWifiInit_ = false;
    }

    if (ownsStaNetif_ && staNetif_ != nullptr) {
        esp_netif_destroy_default_wifi(staNetif_);
        staNetif_ = nullptr;
        ownsStaNetif_ = false;
    }

    eventSink_ = nullptr;
    eventContext_ = nullptr;
    scheduledReconnectDelayMs_ = 0;
    suppressDisconnectEvent_ = false;
    initialized_ = false;
    started_ = false;
    wifiStarted_ = false;

    return ESP_OK;
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

            if (adapter->suppressDisconnectEvent_ && disconnected->reason == WIFI_REASON_ASSOC_LEAVE) {
                adapter->suppressDisconnectEvent_ = false;
                return;
            }
        }

        adapter->suppressDisconnectEvent_ = false;

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

    const size_t ssidLength = strnlen(credentials.ssid, sizeof(credentials.ssid));
    const size_t passwordLength = strnlen(credentials.password, sizeof(credentials.password));

    std::memcpy(wifiConfig.sta.ssid, credentials.ssid, ssidLength);
    std::memcpy(wifiConfig.sta.password, credentials.password, passwordLength);

    result = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (result != ESP_OK) {
        return result;
    }

    return esp_wifi_connect();
}

}  // namespace esp32_wifi_manager