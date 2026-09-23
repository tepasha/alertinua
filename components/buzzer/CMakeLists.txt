idf_component_register(
    SRCS "buzzer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_ledc freertos log esp_timer esp_system
)
