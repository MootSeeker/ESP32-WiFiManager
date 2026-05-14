#include "esp32_wifi_manager/WifiManagerTask.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
}

namespace {

constexpr const char* kTag = "example";

esp32_wifi_manager::WifiManagerTask wifiTask;

void OnWifiStateChanged(esp32_wifi_manager::WifiState newState, void* /* userContext */)
{
    switch (newState) {
    case esp32_wifi_manager::WifiState::kInit:
        ESP_LOGI(kTag, "WiFi: Initializing");
        break;
    case esp32_wifi_manager::WifiState::kConnecting:
        ESP_LOGI(kTag, "WiFi: Connecting...");
        break;
    case esp32_wifi_manager::WifiState::kWaitingToRetry:
        ESP_LOGI(kTag, "WiFi: Connection failed, waiting to retry");
        break;
    case esp32_wifi_manager::WifiState::kConnected: {
        auto status = wifiTask.GetRuntimeStatus();
        ESP_LOGI(kTag, "WiFi: Connected! IP: " IPSTR, IP2STR((esp_ip4_addr_t*)&status.ipAddress));
        break;
    }
    case esp32_wifi_manager::WifiState::kPortal:
        ESP_LOGI(kTag, "WiFi: Provisioning portal active — connect to AP and open http://192.168.4.1");
        break;
    case esp32_wifi_manager::WifiState::kStopped:
        ESP_LOGI(kTag, "WiFi: Stopped");
        break;
    case esp32_wifi_manager::WifiState::kError:
        ESP_LOGE(kTag, "WiFi: Error");
        break;
    }
}

}  // namespace

extern "C" void app_main(void)
{
    // Initialize NVS — required for WiFi credential storage
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS partition issue, erasing and re-initializing");
        nvs_flash_erase();
        result = nvs_flash_init();
    }

    if (result != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(result));
        return;
    }

    // Configure the WiFi manager
    esp32_wifi_manager::WifiManagerConfig config;
    config.apSsidPrefix = "ESP32-Setup-";
    config.maxConnectAttempts = 3;
    config.initialReconnectDelayMs = 2000;
    config.maxReconnectDelayMs = 30000;

    // Initialize and start the WiFi manager task
    result = wifiTask.Init(config, &OnWifiStateChanged, nullptr);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "WiFi manager init failed: %s", esp_err_to_name(result));
        return;
    }

    result = wifiTask.Start();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "WiFi manager start failed: %s", esp_err_to_name(result));
        return;
    }

    ESP_LOGI(kTag, "WiFi manager running. Application main loop active.");

    // Application main loop — the WiFi manager runs in its own task
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        auto state = wifiTask.GetState();
        if (state == esp32_wifi_manager::WifiState::kConnected) {
            ESP_LOGI(kTag, "Application: WiFi connected, doing application work...");
        }
    }
}