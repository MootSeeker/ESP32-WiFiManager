#pragma once

#include <cstdint>

#include "esp_err.h"

extern "C" {
#include "esp_http_server.h"
}

#include "esp32_wifi_manager/WifiManagerEspIdfAdapter.hpp"
#include "esp32_wifi_manager/WifiScanService.hpp"

namespace esp32_wifi_manager {

class CaptivePortalHttp;

struct HttpHandlerContext {
    CaptivePortalHttp* self;
    WifiScanService* scanService;
    WifiManagerEventSink eventSink;
    void* eventContext;
};

class CaptivePortalHttp {
public:
    esp_err_t Start(WifiManagerEventSink eventSink, void* eventContext,
                    WifiScanService& scanService, uint16_t port);
    esp_err_t Stop();
    bool IsRunning() const;

private:
    static esp_err_t HandleGetRoot(httpd_req_t* req);
    static esp_err_t HandleGetScan(httpd_req_t* req);
    static esp_err_t HandlePostConnect(httpd_req_t* req);
    static esp_err_t HandleGetGenerate204(httpd_req_t* req);

    static int UrlDecode(const char* src, int srcLen, char* dst, int dstSize);
    static bool FindFormField(const char* body, int bodyLen,
                              const char* fieldName, char* out, int outSize);
    static int JsonEscapeString(const char* src, char* dst, int dstSize);

    httpd_handle_t server_ = nullptr;
    HttpHandlerContext ctx_{};
    bool running_ = false;
};

}  // namespace esp32_wifi_manager
