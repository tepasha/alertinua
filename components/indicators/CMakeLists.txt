idf_component_register(
    SRCS "indicators.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer log app_state
)
