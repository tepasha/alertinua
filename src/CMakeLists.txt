file(GLOB_RECURSE SRC_SOURCES "*.cpp" "*.c")

idf_component_register(
    SRC_DIRS 
        "."
        "../include/map"
        "../include/env"
        "../include/scraping"
        "../include/button"
        "../include/wifi_manager"
    INCLUDE_DIRS 
        "."
        "../include/map"
        "../include/env"
        "../include/scraping"
        "../include/button"
        "../include/wifi_manager"
)
