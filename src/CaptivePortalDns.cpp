#include "esp32_wifi_manager/CaptivePortalDns.hpp"

#include <cerrno>
#include <cstring>

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
}

namespace esp32_wifi_manager {

namespace {

constexpr const char* kTag = "dns_hijack";
constexpr int kDnsPort = 53;
constexpr int kMaxDnsPacketSize = 512;
constexpr int kDnsHeaderSize = 12;
constexpr int kSocketTimeoutMs = 500;
constexpr uint32_t kTtlSeconds = 60;
constexpr int kStopPollIntervalMs = 50;
constexpr int kStopTimeoutMs = 1000;

}  // namespace

esp_err_t CaptivePortalDns::Start(uint32_t apIpAddress)
{
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }

    apIpAddress_ = apIpAddress;

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) {
        ESP_LOGE(kTag, "Failed to create UDP socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(kDnsPort);

    if (bind(socket_, reinterpret_cast<struct sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        ESP_LOGE(kTag, "Failed to bind socket to port %d: errno %d", kDnsPort, errno);
        close(socket_);
        socket_ = -1;
        return ESP_FAIL;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = kSocketTimeoutMs * 1000;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(kTag, "Failed to set socket timeout: errno %d", errno);
        close(socket_);
        socket_ = -1;
        return ESP_FAIL;
    }

    running_ = true;

    TaskHandle_t handle = nullptr;
    BaseType_t created = xTaskCreate(
        &CaptivePortalDns::DnsTask,
        "dns_hijack",
        4096,
        this,
        3,
        &handle);

    if (created != pdPASS) {
        ESP_LOGE(kTag, "Failed to create DNS task");
        running_ = false;
        close(socket_);
        socket_ = -1;
        return ESP_ERR_NO_MEM;
    }

    taskHandle_ = static_cast<void*>(handle);
    return ESP_OK;
}

esp_err_t CaptivePortalDns::Stop()
{
    if (!running_) {
        return ESP_ERR_INVALID_STATE;
    }

    running_ = false;

    int waited = 0;
    while (taskHandle_ != nullptr && waited < kStopTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(kStopPollIntervalMs));
        waited += kStopPollIntervalMs;
    }

    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }

    taskHandle_ = nullptr;
    return ESP_OK;
}

bool CaptivePortalDns::IsRunning() const
{
    return running_;
}

void CaptivePortalDns::DnsTask(void* arg)
{
    auto* self = static_cast<CaptivePortalDns*>(arg);
    uint8_t buffer[kMaxDnsPacketSize];

    while (self->running_) {
        struct sockaddr_in sourceAddr;
        socklen_t addrLen = sizeof(sourceAddr);

        int len = recvfrom(self->socket_, buffer, sizeof(buffer), 0,
                           reinterpret_cast<struct sockaddr*>(&sourceAddr), &addrLen);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ESP_LOGE(kTag, "recvfrom failed: errno %d", errno);
            continue;
        }

        if (len < kDnsHeaderSize) {
            continue;
        }

        self->ProcessDnsQuery(self->socket_, buffer, len, &sourceAddr, addrLen);
    }

    close(self->socket_);
    self->socket_ = -1;
    self->taskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

void CaptivePortalDns::ProcessDnsQuery(int sock, const uint8_t* queryBuffer, int queryLength,
                                        struct sockaddr_in* sourceAddr, socklen_t addrLen)
{
    uint8_t response[kMaxDnsPacketSize];
    int offset = 0;

    // Copy Transaction ID from query (bytes 0-1)
    response[offset++] = queryBuffer[0];
    response[offset++] = queryBuffer[1];

    // Flags: standard response, no error (0x8180)
    response[offset++] = 0x81;
    response[offset++] = 0x80;

    // QDCOUNT: copy from query (bytes 4-5)
    uint16_t qdcount = (static_cast<uint16_t>(queryBuffer[4]) << 8) | queryBuffer[5];
    response[offset++] = queryBuffer[4];
    response[offset++] = queryBuffer[5];

    // ANCOUNT: same as QDCOUNT
    response[offset++] = queryBuffer[4];
    response[offset++] = queryBuffer[5];

    // NSCOUNT: 0
    response[offset++] = 0x00;
    response[offset++] = 0x00;

    // ARCOUNT: 0
    response[offset++] = 0x00;
    response[offset++] = 0x00;

    // Copy the question section from the query
    int questionStart = kDnsHeaderSize;
    int questionEnd = questionStart;

    for (uint16_t q = 0; q < qdcount; ++q) {
        // Walk labels until null terminator
        while (questionEnd < queryLength && queryBuffer[questionEnd] != 0) {
            uint8_t labelLen = queryBuffer[questionEnd];
            questionEnd += 1 + labelLen;
            if (questionEnd > queryLength) {
                ESP_LOGE(kTag, "Malformed DNS query: label exceeds packet");
                return;
            }
        }
        if (questionEnd >= queryLength) {
            ESP_LOGE(kTag, "Malformed DNS query: missing null terminator");
            return;
        }
        questionEnd += 1;  // null terminator
        questionEnd += 4;  // QTYPE (2) + QCLASS (2)
        if (questionEnd > queryLength) {
            ESP_LOGE(kTag, "Malformed DNS query: truncated question");
            return;
        }
    }

    int questionSectionLen = questionEnd - questionStart;
    if (offset + questionSectionLen > kMaxDnsPacketSize) {
        ESP_LOGE(kTag, "Question section too large for response buffer");
        return;
    }
    memcpy(&response[offset], &queryBuffer[questionStart], questionSectionLen);
    offset += questionSectionLen;

    // Append one answer per question
    // apIpAddress_ is expected in network byte order (matching ESP-IDF convention)
    for (uint16_t q = 0; q < qdcount; ++q) {
        if (offset + 16 > kMaxDnsPacketSize) {
            ESP_LOGE(kTag, "Response buffer full, cannot add answer %u", q);
            break;
        }

        // Name pointer to the first question name (0xC00C)
        response[offset++] = 0xC0;
        response[offset++] = 0x0C;

        // Type A (0x0001)
        response[offset++] = 0x00;
        response[offset++] = 0x01;

        // Class IN (0x0001)
        response[offset++] = 0x00;
        response[offset++] = 0x01;

        // TTL (60 seconds)
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = static_cast<uint8_t>(kTtlSeconds);

        // RDLENGTH (4 bytes for IPv4)
        response[offset++] = 0x00;
        response[offset++] = 0x04;

        // RDATA: AP IP address (already in network byte order)
        memcpy(&response[offset], &apIpAddress_, 4);
        offset += 4;
    }

    int sent = sendto(sock, response, offset, 0,
                      reinterpret_cast<struct sockaddr*>(sourceAddr), addrLen);
    if (sent < 0) {
        ESP_LOGE(kTag, "sendto failed: errno %d", errno);
    }
}

}  // namespace esp32_wifi_manager
