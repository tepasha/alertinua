idf_component_register(
    SRCS "battery.c" "battery_percent.c"
    INCLUDE_DIRS "."
    REQUIRES adc_shared esp_driver_gpio log
)
