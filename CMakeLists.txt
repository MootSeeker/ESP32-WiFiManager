idf_component_register(
    SRCS "src/WifiManager.cpp"
         "src/WifiCredentialStore.cpp"
         "src/WifiManagerEspIdfAdapter.cpp"
         "src/WifiManagerStateMachine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_event esp_netif esp_wifi log nvs_flash
)