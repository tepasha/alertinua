idf_component_register(
    SRCS "button.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos log esp_timer esp_system
)
