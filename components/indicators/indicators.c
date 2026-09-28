#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "app_state.h"
#include "indicators.h"

#ifndef CONFIG_ALERTINUA_INDICATOR_PERIOD_MS
#define CONFIG_ALERTINUA_INDICATOR_PERIOD_MS 500
#endif

/* Світлодіоди через ШІМ, а не просто GPIO 0/1 - щоб горіли тьмяніше.
 * Таймер і канали окремі від зумера (TIMER_0/CHANNEL_0) і підсвітки
 * (TIMER_1/CHANNEL_1). 5 кГц - значно вище за поріг помітного мерехтіння. */
#define LED_LEDC_TIMER      LEDC_TIMER_2
#define LED_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LED_DUTY_RES        LEDC_TIMER_10_BIT /* 0..1023 */
#define LED_FREQ_HZ         5000
#define STATUS_LED_CHANNEL  LEDC_CHANNEL_2
#define WIFI_LED_CHANNEL    LEDC_CHANNEL_3
#define LED_DUTY_ON         512 /* 50% від повної яскравості */

static const char *TAG = "INDICATORS";
static esp_timer_handle_t s_timer = NULL;
static bool s_blink_phase = false;

static void led_set(ledc_channel_t channel, bool on) {
    ledc_set_duty(LED_LEDC_MODE, channel, on ? LED_DUTY_ON : 0);
    ledc_update_duty(LED_LEDC_MODE, channel);
}

static void indicators_timer_cb(void *arg) {
    (void)arg;
    s_blink_phase = !s_blink_phase;

    bool alarm = app_state_is_alarm_active();
    led_set(STATUS_LED_CHANNEL, alarm && s_blink_phase);

    switch (app_state_get_wifi_status()) {
        case WIFI_STATUS_CONNECTED:
            led_set(WIFI_LED_CHANNEL, true);
            break;
        case WIFI_STATUS_CONNECTING:
            led_set(WIFI_LED_CHANNEL, s_blink_phase);
            break;
        case WIFI_STATUS_PROVISIONING:
        case WIFI_STATUS_DISCONNECTED:
        default:
            led_set(WIFI_LED_CHANNEL, false);
            break;
    }
}

static void led_channel_init(int gpio, ledc_channel_t channel) {
    const ledc_channel_config_t channel_cfg = {
        .gpio_num = gpio,
        .speed_mode = LED_LEDC_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LED_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    esp_err_t err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config (GPIO%d): %s", gpio, esp_err_to_name(err));
    }
}

void indicators_init(void) {
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LED_LEDC_MODE,
        .duty_resolution = LED_DUTY_RES,
        .timer_num = LED_LEDC_TIMER,
        .freq_hz = LED_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
    }
    led_channel_init(STATUS_LED_GPIO, STATUS_LED_CHANNEL);
    led_channel_init(WIFI_LED_GPIO, WIFI_LED_CHANNEL);

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
