idf_component_register(
    SRCS "render_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log esp_system esp_timer heap esp_lcd map fetch_task buzzer_task
)
