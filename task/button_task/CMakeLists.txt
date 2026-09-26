idf_component_register(
    SRCS "button_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system esp_timer button
)
