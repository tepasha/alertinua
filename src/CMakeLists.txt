file(GLOB_RECURSE SRC_SOURCES "*.cpp" "*.c")

idf_component_register(
    SRC_DIRS
        "."
    INCLUDE_DIRS
        "."
        "../components/env"
        "../components/scraping"
        "../components/wifi_manager"
        "../components/map"
        "../components/button"
)
