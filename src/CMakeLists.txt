file(GLOB_RECURSE SRC_SOURCES "*.cpp" "*.c")

idf_component_register(
    SRC_DIRS
        "."
        "../include/wifi_manager"
        "../include/map"
        "../include/button"
    INCLUDE_DIRS
        "."
        "../include/env"
        "../include/scraping"
)
