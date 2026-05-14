extern "C" {
#include "esp_wifi.h"
}

#include <cstring>

#include "esp_log.h"

#include "esp32_wifi_manager/WifiScanService.hpp"

static const char* kTag = "wifi_scan";

namespace esp32_wifi_manager {

esp_err_t WifiScanService::StartScan() {
    ESP_LOGI(kTag, "Starting WiFi scan");
    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Scan failed: %s", esp_err_to_name(err));
    }
    return err;
}

uint16_t WifiScanService::GetResultCount() const {
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    return count;
}

uint16_t WifiScanService::GetResults(WifiScanResult* outResults, uint16_t maxCount) const {
    static constexpr uint16_t kMaxRecords = 20;
    if (maxCount > kMaxRecords) {
        maxCount = kMaxRecords;
    }

    wifi_ap_record_t records[kMaxRecords];
    uint16_t count = maxCount;
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to get scan results: %s", esp_err_to_name(err));
        return 0;
    }

    for (uint16_t i = 0; i < count; ++i) {
        std::memcpy(outResults[i].ssid, records[i].ssid, sizeof(outResults[i].ssid));
        outResults[i].ssid[32] = '\0';
        outResults[i].rssi = records[i].rssi;
        outResults[i].authMode = static_cast<uint8_t>(records[i].authmode);
        outResults[i].channel = records[i].primary;
    }

    ESP_LOGI(kTag, "Scan returned %u APs", count);
    return count;
}

}  // namespace esp32_wifi_manager
