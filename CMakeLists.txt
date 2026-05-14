idf_component_register(
    SRCS "src/WifiManager.cpp"
         "src/WifiCredentialStore.cpp"
         "src/WifiManagerEspIdfAdapter.cpp"
         "src/WifiManagerStateMachine.cpp"
         "src/WifiManagerTask.cpp"
         "src/WifiScanService.cpp"
         "src/CaptivePortalDns.cpp"
         "src/CaptivePortalHttp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_event esp_http_server esp_netif esp_wifi freertos log lwip nvs_flash
    EMBED_FILES "resources/portal.html"
)