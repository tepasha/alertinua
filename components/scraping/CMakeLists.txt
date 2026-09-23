idf_component_register(
    SRCS "scraping.c" "alerts_parser.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client mbedtls json log map
)
