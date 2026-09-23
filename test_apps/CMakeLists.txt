idf_component_register(
    SRCS
        "test_main.c"
        "test_alerts_parser.c"
        "test_brightness_ctrl.c"
        "test_app_state.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES unity app_state brightness_ctrl scraping map
)
