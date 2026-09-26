idf_component_register(
    SRCS "fetch_task.c"
    INCLUDE_DIRS "."
    # env/env_config.h - згенерований з .env (див. env/env.py)
    PRIV_INCLUDE_DIRS "../../env"
    REQUIRES freertos log esp_system esp_timer app_state scraping settings
)
