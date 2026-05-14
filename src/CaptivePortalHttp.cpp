#include "esp32_wifi_manager/CaptivePortalHttp.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_log.h"
}

#include "portal_html.h"

namespace esp32_wifi_manager {

namespace {

constexpr const char* kTag = "portal_http";
constexpr int kMaxPostBodySize = 256;
constexpr int kMaxScanResults = 20;
constexpr int kJsonBufSize = 2048;

}  // namespace

esp_err_t CaptivePortalHttp::Start(WifiManagerEventSink eventSink,
                                   void* eventContext,
                                   WifiScanService& scanService,
                                   uint16_t port)
{
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }

    ctx_.self = this;
    ctx_.scanService = &scanService;
    ctx_.eventSink = eventSink;
    ctx_.eventContext = eventContext;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 4;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uriRoot = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = HandleGetRoot,
        .user_ctx = &ctx_,
    };

    const httpd_uri_t uriScan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = HandleGetScan,
        .user_ctx = &ctx_,
    };

    const httpd_uri_t uriConnect = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = HandlePostConnect,
        .user_ctx = &ctx_,
    };

    const httpd_uri_t uriGenerate204 = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = HandleGetGenerate204,
        .user_ctx = &ctx_,
    };

    httpd_register_uri_handler(server_, &uriRoot);
    httpd_register_uri_handler(server_, &uriScan);
    httpd_register_uri_handler(server_, &uriConnect);
    httpd_register_uri_handler(server_, &uriGenerate204);

    running_ = true;
    ESP_LOGI(kTag, "HTTP server started on port %u", port);
    return ESP_OK;
}

esp_err_t CaptivePortalHttp::Stop()
{
    if (!running_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_stop(server_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to stop HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    server_ = nullptr;
    running_ = false;
    ESP_LOGI(kTag, "HTTP server stopped");
    return ESP_OK;
}

bool CaptivePortalHttp::IsRunning() const
{
    return running_;
}

// ---------------------------------------------------------------------------
// URI Handlers
// ---------------------------------------------------------------------------

esp_err_t CaptivePortalHttp::HandleGetRoot(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_html_start,
                    static_cast<ssize_t>(portal_html_end - portal_html_start));
    return ESP_OK;
}

esp_err_t CaptivePortalHttp::HandleGetScan(httpd_req_t* req)
{
    auto* ctx = static_cast<HttpHandlerContext*>(req->user_ctx);

    esp_err_t err = ctx->scanService->StartScan();
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    WifiScanResult results[kMaxScanResults];
    uint16_t count = ctx->scanService->GetResults(results, kMaxScanResults);

    char json[kJsonBufSize];
    int pos = 0;
    json[pos++] = '[';

    for (uint16_t i = 0; i < count && pos < kJsonBufSize - 100; ++i) {
        if (i > 0) {
            json[pos++] = ',';
        }

        char escapedSsid[100];
        JsonEscapeString(results[i].ssid, escapedSsid, sizeof(escapedSsid));

        int written = snprintf(
            json + pos, static_cast<size_t>(kJsonBufSize - pos),
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%u,\"ch\":%u}",
            escapedSsid,
            static_cast<int>(results[i].rssi),
            static_cast<unsigned>(results[i].authMode),
            static_cast<unsigned>(results[i].channel));

        if (written > 0 && pos + written < kJsonBufSize) {
            pos += written;
        } else {
            break;
        }
    }

    json[pos++] = ']';
    json[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, pos);
    return ESP_OK;
}

esp_err_t CaptivePortalHttp::HandlePostConnect(httpd_req_t* req)
{
    auto* ctx = static_cast<HttpHandlerContext*>(req->user_ctx);

    int contentLen = req->content_len;
    if (contentLen <= 0 || contentLen > kMaxPostBodySize) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"status\":\"error\",\"message\":\"Invalid request body\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char buf[kMaxPostBodySize + 1];
    int received = httpd_req_recv(req, buf, static_cast<size_t>(contentLen));
    if (received <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"status\":\"error\",\"message\":\"Failed to read body\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    buf[received] = '\0';

    char ssid[33] = {};
    char password[65] = {};

    if (!FindFormField(buf, received, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"status\":\"error\",\"message\":\"Missing SSID\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // password is optional (open networks)
    FindFormField(buf, received, "password", password, sizeof(password));

    if (ssid[0] == '\0') {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"status\":\"error\",\"message\":\"SSID must not be empty\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    WifiManagerEvent event{};
    event.type = WifiManagerEventType::kCredentialsReceived;
    memcpy(event.credentials.ssid, ssid, sizeof(event.credentials.ssid));
    memcpy(event.credentials.password, password, sizeof(event.credentials.password));

    esp_err_t err = ctx->eventSink(event, ctx->eventContext);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to deliver credentials event: %s",
                 esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req,
            "{\"status\":\"error\",\"message\":\"Internal error\"}",
            HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(kTag, "Credentials received for SSID: %s", ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
                    "{\"status\":\"ok\",\"message\":\"Connecting...\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t CaptivePortalHttp::HandleGetGenerate204(httpd_req_t* req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Utility: URL-decode
// ---------------------------------------------------------------------------

int CaptivePortalHttp::UrlDecode(const char* src, int srcLen,
                                 char* dst, int dstSize)
{
    int di = 0;
    for (int si = 0; si < srcLen && di < dstSize - 1; ++si) {
        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && si + 2 < srcLen
                   && std::isxdigit(static_cast<unsigned char>(src[si + 1]))
                   && std::isxdigit(static_cast<unsigned char>(src[si + 2]))) {
            char hex[3] = {src[si + 1], src[si + 2], '\0'};
            dst[di++] = static_cast<char>(strtol(hex, nullptr, 16));
            si += 2;
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
    return di;
}

// ---------------------------------------------------------------------------
// Utility: extract a URL-encoded form field
// ---------------------------------------------------------------------------

bool CaptivePortalHttp::FindFormField(const char* body, int bodyLen,
                                      const char* fieldName,
                                      char* out, int outSize)
{
    int nameLen = static_cast<int>(strlen(fieldName));
    const char* pos = body;
    const char* end = body + bodyLen;

    while (pos < end) {
        // Check if current position matches "fieldName="
        if (end - pos > nameLen && memcmp(pos, fieldName, static_cast<size_t>(nameLen)) == 0
            && pos[nameLen] == '=') {
            const char* valueStart = pos + nameLen + 1;
            const char* valueEnd = static_cast<const char*>(
                memchr(valueStart, '&', static_cast<size_t>(end - valueStart)));
            if (valueEnd == nullptr) {
                valueEnd = end;
            }
            int rawLen = static_cast<int>(valueEnd - valueStart);
            UrlDecode(valueStart, rawLen, out, outSize);
            return true;
        }
        // Advance to next '&'
        const char* amp = static_cast<const char*>(
            memchr(pos, '&', static_cast<size_t>(end - pos)));
        if (amp == nullptr) {
            break;
        }
        pos = amp + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Utility: JSON-escape a string (no allocation)
// ---------------------------------------------------------------------------

int CaptivePortalHttp::JsonEscapeString(const char* src, char* dst, int dstSize)
{
    int di = 0;
    for (int si = 0; src[si] != '\0' && di < dstSize - 6; ++si) {
        unsigned char c = static_cast<unsigned char>(src[si]);
        if (c == '"') {
            dst[di++] = '\\';
            dst[di++] = '"';
        } else if (c == '\\') {
            dst[di++] = '\\';
            dst[di++] = '\\';
        } else if (c == '\n') {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\t') {
            dst[di++] = '\\';
            dst[di++] = 't';
        } else if (c < 0x20) {
            int written = snprintf(dst + di, static_cast<size_t>(dstSize - di),
                                   "\\u%04x", c);
            if (written > 0) {
                di += written;
            }
        } else {
            dst[di++] = static_cast<char>(c);
        }
    }
    dst[di] = '\0';
    return di;
}

}  // namespace esp32_wifi_manager
