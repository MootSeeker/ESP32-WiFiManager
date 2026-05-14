#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"

#include "esp32_wifi_manager/WifiManager.hpp"
#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiManagerTask {
public:
    WifiManagerTask() = default;
    ~WifiManagerTask();

    WifiManagerTask(const WifiManagerTask&) = delete;
    WifiManagerTask& operator=(const WifiManagerTask&) = delete;

    esp_err_t Init(const WifiManagerConfig& config,
                   WifiStateChangedCallback stateChanged = nullptr,
                   void* userContext = nullptr);

    esp_err_t Start(uint32_t taskPriority = 5, uint32_t stackSize = 4096);
    esp_err_t Stop();

    void ForceProvisioning();
    WifiState GetState() const;
    WifiRuntimeStatus GetRuntimeStatus() const;
    bool IsRunning() const;

private:
    static void TaskFunction(void* param);
    void Run();

    static constexpr uint32_t kTickIntervalMs = 100;
    static constexpr uint32_t kQueueCapacity = 8;

    WifiManager manager_{};
    void* queue_ = nullptr;
    void* taskHandle_ = nullptr;
    std::atomic<bool> running_{false};
};

}  // namespace esp32_wifi_manager
