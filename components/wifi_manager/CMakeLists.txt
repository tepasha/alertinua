idf_component_register(
    SRCS "wifi_manager.c" "wifi_creds.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_http_server nvs_flash driver
              freertos log esp_system app_state
)
