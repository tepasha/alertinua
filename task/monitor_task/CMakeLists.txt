idf_component_register(
    SRCS "monitor_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system app_state
)
