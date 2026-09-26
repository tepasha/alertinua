idf_component_register(
    SRCS "brightness_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system light_sensor backlight app_state brightness_ctrl
)
