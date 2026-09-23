#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "app_state.h"
#include "indicators.h"

#ifndef CONFIG_ALERTINUA_INDICATOR_PERIOD_MS
#define CONFIG_ALERTINUA_INDICATOR_PERIOD_MS 500
#endif

static const char *TAG = "INDICATORS";
static esp_timer_handle_t s_timer = NULL;
static bool s_blink_phase = false;

static void indicators_timer_cb(void *arg) {
    (void)arg;
    s_blink_phase = !s_blink_phase;

    bool alarm = app_state_is_alarm_active();
    gpio_set_level(STATUS_LED_GPIO, alarm ? (int)s_blink_phase : 0);

    switch (app_state_get_wifi_status()) {
        case WIFI_STATUS_CONNECTED:
            gpio_set_level(WIFI_LED_GPIO, 1);
            break;
        case WIFI_STATUS_CONNECTING:
            gpio_set_level(WIFI_LED_GPIO, (int)s_blink_phase);
            break;
        case WIFI_STATUS_PROVISIONING:
        case WIFI_STATUS_DISCONNECTED:
        default:
            gpio_set_level(WIFI_LED_GPIO, 0);
            break;
    }
}

void indicators_init(void) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO) | (1ULL << WIFI_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
    }
    gpio_set_level(STATUS_LED_GPIO, 0);
    gpio_set_level(WIFI_LED_GPIO, 0);

    const esp_timer_create_args_t timer_args = {
        .callback = &indicators_timer_cb,
        .name = "indicators_tick",
    };
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(err));
        return;
    }

    err = esp_timer_start_periodic(s_timer, (uint64_t)CONFIG_ALERTINUA_INDICATOR_PERIOD_MS * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(err));
    }
}
