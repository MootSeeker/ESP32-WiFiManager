#pragma once

#include <cstdint>

#include "esp_err.h"

#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiScanService {
public:
    esp_err_t StartScan();
    uint16_t GetResultCount() const;
    uint16_t GetResults(WifiScanResult* outResults, uint16_t maxCount) const;
};

}  // namespace esp32_wifi_manager
