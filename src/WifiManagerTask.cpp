#include "esp32_wifi_manager/WifiManagerTask.hpp"

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
}

namespace esp32_wifi_manager {

namespace {

constexpr const char* kTag = "wifi_task";

}  // namespace

WifiManagerTask::~WifiManagerTask()
{
    if (running_.load()) {
        Stop();
    }
}

esp_err_t WifiManagerTask::Init(const WifiManagerConfig& config,
                                WifiStateChangedCallback stateChanged,
                                void* userContext)
{
    if (running_.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    return manager_.Init(config, stateChanged, userContext);
}

esp_err_t WifiManagerTask::Start(uint32_t taskPriority, uint32_t stackSize)
{
    if (running_.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    auto queue = xQueueCreate(kQueueCapacity, sizeof(WifiManagerEvent));
    if (queue == nullptr) {
        ESP_LOGE(kTag, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }
    queue_ = static_cast<void*>(queue);

    manager_.SetExternalQueue(queue_);

    esp_err_t result = manager_.Start();
    if (result != ESP_OK) {
        manager_.SetExternalQueue(nullptr);
        vQueueDelete(queue);
        queue_ = nullptr;
        return result;
    }

    running_.store(true);

    TaskHandle_t handle = nullptr;
    BaseType_t created = xTaskCreate(
        &WifiManagerTask::TaskFunction,
        "wifi_task",
        stackSize,
        this,
        taskPriority,
        &handle);

    if (created != pdPASS) {
        ESP_LOGE(kTag, "Failed to create FreeRTOS task");
        running_.store(false);
        manager_.Stop();
        manager_.SetExternalQueue(nullptr);
        vQueueDelete(static_cast<QueueHandle_t>(queue_));
        queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    taskHandle_ = static_cast<void*>(handle);
    return ESP_OK;
}

esp_err_t WifiManagerTask::Stop()
{
    if (!running_.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    running_.store(false);

    // Send a dummy event to unblock xQueueReceive
    if (queue_ != nullptr) {
        WifiManagerEvent dummy{};
        dummy.type = WifiManagerEventType::kProvisioningRequested;
        xQueueSendToBack(static_cast<QueueHandle_t>(queue_), &dummy, 0);
    }

    // Wait for the task to exit — it calls vTaskDelete(nullptr) at the end
    // Poll briefly since FreeRTOS has no join primitive
    for (int i = 0; i < 50; ++i) {
        if (taskHandle_ == nullptr) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    esp_err_t result = manager_.Stop();

    manager_.SetExternalQueue(nullptr);

    if (queue_ != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(queue_));
        queue_ = nullptr;
    }

    return result;
}

void WifiManagerTask::ForceProvisioning()
{
    manager_.ForceProvisioning();
}

WifiState WifiManagerTask::GetState() const
{
    return manager_.GetState();
}

WifiRuntimeStatus WifiManagerTask::GetRuntimeStatus() const
{
    return manager_.GetRuntimeStatus();
}

bool WifiManagerTask::IsRunning() const
{
    return running_.load();
}

void WifiManagerTask::TaskFunction(void* param)
{
    auto* self = static_cast<WifiManagerTask*>(param);
    self->Run();
    self->taskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

void WifiManagerTask::Run()
{
    auto queue = static_cast<QueueHandle_t>(queue_);
    ESP_LOGI(kTag, "Task started");

    while (running_.load()) {
        WifiManagerEvent event{};
        BaseType_t received = xQueueReceive(queue, &event, pdMS_TO_TICKS(kTickIntervalMs));

        if (!running_.load()) {
            break;
        }

        if (received == pdTRUE) {
            esp_err_t result = manager_.DispatchEvent(event);
            if (result != ESP_OK) {
                ESP_LOGW(kTag, "DispatchEvent failed: %s", esp_err_to_name(result));
            }
        } else {
            manager_.AdvanceRetryTimer(kTickIntervalMs);
        }
    }

    ESP_LOGI(kTag, "Task stopping");
}

}  // namespace esp32_wifi_manager
