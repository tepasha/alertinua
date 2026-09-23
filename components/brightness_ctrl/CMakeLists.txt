idf_component_register(
    SRCS "brightness_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system light_sensor backlight app_state
)
