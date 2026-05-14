#include "esp32_wifi_manager/WifiCredentialStore.hpp"

extern "C" {
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
}

namespace {

constexpr const char* kTag = "wifi_cred";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";

}  // namespace

namespace esp32_wifi_manager {

WifiCredentialStore::WifiCredentialStore(const char* nvsNamespace)
    : nvsNamespace_(nvsNamespace != nullptr ? nvsNamespace : "wifi_mgr")
{
}

bool WifiCredentialStore::Load(WifiCredentials& outCredentials) const
{
    outCredentials = {};

    nvs_handle_t handle;
    if (nvs_open(nvsNamespace_, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t ssidLength = sizeof(outCredentials.ssid);
    size_t passwordLength = sizeof(outCredentials.password);

    const bool loaded = (nvs_get_str(handle, kKeySsid, outCredentials.ssid, &ssidLength) == ESP_OK) &&
                        (nvs_get_str(handle, kKeyPass, outCredentials.password, &passwordLength) == ESP_OK) &&
                        (outCredentials.ssid[0] != '\0');

    nvs_close(handle);

    if (loaded) {
        ESP_LOGI(kTag, "Loaded credentials for SSID: '%s'", outCredentials.ssid);
    }

    return loaded;
}

bool WifiCredentialStore::Save(const WifiCredentials& credentials) const
{
    nvs_handle_t handle;
    if (nvs_open(nvsNamespace_, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to open NVS for write");
        return false;
    }

    const bool saved = (nvs_set_str(handle, kKeySsid, credentials.ssid) == ESP_OK) &&
                       (nvs_set_str(handle, kKeyPass, credentials.password) == ESP_OK) &&
                       (nvs_commit(handle) == ESP_OK);

    nvs_close(handle);

    if (saved) {
        ESP_LOGI(kTag, "Credentials saved for SSID: '%s'", credentials.ssid);
    } else {
        ESP_LOGE(kTag, "Failed to save credentials");
    }

    return saved;
}

void WifiCredentialStore::Clear() const
{
    nvs_handle_t handle;
    if (nvs_open(nvsNamespace_, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_erase_key(handle, kKeySsid);
    nvs_erase_key(handle, kKeyPass);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(kTag, "Credentials cleared");
}

}  // namespace esp32_wifi_manager