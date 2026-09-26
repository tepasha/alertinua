idf_component_register(
    SRCS "main.c"

    INCLUDE_DIRS
        "."
        "../env"

    REQUIRES
        freertos
        log
        esp_common
        esp_system
        nvs_flash
        app_state
        indicators
        wifi_manager
        wifi_task
        fetch_task
        render_task
        button_task
        buzzer_task
        brightness_task
        monitor_task
)
