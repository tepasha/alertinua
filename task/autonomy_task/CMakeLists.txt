idf_component_register(
    SRCS "autonomy_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system esp_hw_support esp_wifi esp_driver_gpio
             app_state backlight button settings wifi_manager
             buzzer_task fetch_task render_task
)
