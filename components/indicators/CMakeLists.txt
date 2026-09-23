idf_component_register(
    SRCS "indicators.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer log app_state
)
