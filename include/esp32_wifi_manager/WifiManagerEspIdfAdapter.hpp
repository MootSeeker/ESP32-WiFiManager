#pragma once

extern "C" {
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
}

#include "esp_err.h"

#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

using WifiManagerEventSink = esp_err_t (*)(const WifiManagerEvent& event, void* eventContext);

class WifiManagerEspIdfAdapter {
public:
    esp_err_t Init(const WifiManagerConfig& config,
                   WifiManagerEventSink eventSink,
                   void* eventContext);

    esp_err_t Start();
    esp_err_t ApplyState(WifiState state,
                         const WifiCredentials& credentials,
                         uint32_t reconnectDelayMs);
    void DetachEventSink();
    esp_err_t Stop();
    esp_err_t Deinit();

    bool IsInitialized() const;

private:
    static void EventHandler(void* arg,
                             esp_event_base_t eventBase,
                             int32_t eventId,
                             void* eventData);

    esp_err_t EmitEvent(const WifiManagerEvent& event);
    esp_err_t EnsureWifiStarted();
    esp_err_t ConnectStation(const WifiCredentials& credentials);

    WifiManagerConfig config_{};
    WifiManagerEventSink eventSink_ = nullptr;
    void* eventContext_ = nullptr;
    esp_netif_t* staNetif_ = nullptr;
    esp_event_handler_instance_t wifiEventHandler_{};
    esp_event_handler_instance_t ipEventHandler_{};
    uint32_t scheduledReconnectDelayMs_ = 0;
    bool suppressDisconnectEvent_ = false;
    bool ownsStaNetif_ = false;
    bool ownsWifiInit_ = false;
    bool wifiHandlerRegistered_ = false;
    bool ipHandlerRegistered_ = false;
    bool initialized_ = false;
    bool started_ = false;
    bool wifiStarted_ = false;
};

}  // namespace esp32_wifi_manager