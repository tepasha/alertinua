idf_component_register(
    SRCS "backlight.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ledc log
)
