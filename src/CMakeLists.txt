file(GLOB_RECURSE SRC_SOURCES "*.cpp" "*.c")

idf_component_register(
    SRC_DIRS
        "."
        "../components/buzzer"
        "../components/map"
        "../components/button"
        "../components/wifi_manager"
        "../components/scraping"
    INCLUDE_DIRS
        "."
        "../components/env"
)
