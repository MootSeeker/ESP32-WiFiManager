#pragma once

#include <cstdint>

#include "esp_err.h"

namespace esp32_wifi_manager {

class CaptivePortalDns {
public:
    esp_err_t Start(uint32_t apIpAddress);
    esp_err_t Stop();
    bool IsRunning() const;

private:
    static void DnsTask(void* arg);
    void ProcessDnsQuery(int sock, const uint8_t* queryBuffer, int queryLength,
                         struct sockaddr_in* sourceAddr, socklen_t addrLen);

    uint32_t apIpAddress_ = 0;
    void* taskHandle_ = nullptr;
    int socket_ = -1;
    bool running_ = false;
};

}  // namespace esp32_wifi_manager
