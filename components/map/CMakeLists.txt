idf_component_register(
    SRCS "map_render.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_lcd log
)
