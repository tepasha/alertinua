idf_component_register(
    SRCS "wifi_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system wifi_manager app_state
)
