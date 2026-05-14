#include "esp32_wifi_manager/WifiManager.hpp"

using namespace esp32_wifi_manager;

namespace {

void OnWifiStateChanged(WifiState, void*)
{
}

WifiManager wifiManager;

}  // namespace

extern "C" void app_main(void)
{
    WifiManagerConfig config;
    wifiManager.Init(config, &OnWifiStateChanged, nullptr);
    wifiManager.Start();
}