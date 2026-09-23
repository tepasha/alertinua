idf_component_register(
    SRCS "main.c"

    SRC_DIRS
        "."
        "../components/map"
        "../components/button"
        "../components/buzzer"
        "../components/wifi_manager"
        "../components/scraping"

    INCLUDE_DIRS
        "."
        "../components/app_state"
        "../components/indicators"
        "../components/brightness_ctrl"
        "../env"
        "../components/wifi_manager"
        "../components/buzzer"
        "../components/map"
        "../components/button"
        "../components/scraping"

    REQUIRES
        freertos
        log
        esp_common
        esp_system
        esp_timer
        nvs_flash
        heap
        esp_lcd
        app_state
        button
        buzzer
        indicators
        brightness_ctrl
        wifi_manager
        scraping
        map
)