idf_component_register(
    SRCS "adc_shared.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc freertos log
)
