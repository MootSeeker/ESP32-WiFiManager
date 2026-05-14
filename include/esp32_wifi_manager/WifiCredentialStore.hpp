#pragma once

#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiCredentialStore {
public:
    explicit WifiCredentialStore(const char* nvsNamespace = "wifi_mgr");

    bool Load(WifiCredentials& outCredentials) const;
    bool Save(const WifiCredentials& credentials) const;
    void Clear() const;

private:
    const char* nvsNamespace_;
};

}  // namespace esp32_wifi_manager