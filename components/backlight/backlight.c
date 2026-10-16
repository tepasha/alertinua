#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "esp_err.h"
#include "esp_log.h"

#include "backlight.h"

#define BACKLIGHT_LEDC_TIMER   LEDC_TIMER_1    // окремий від зумера (LEDC_TIMER_0)
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_1  // окремий від зумера (LEDC_CHANNEL_0)
#define BACKLIGHT_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BACKLIGHT_DUTY_RES     LEDC_TIMER_10_BIT // 0..1023
#define BACKLIGHT_FREQ_HZ      5000              // вище за поріг мерехтіння, що помітне оком

static const char *TAG = "BACKLIGHT";
static uint16_t s_max_duty = (1 << 10) - 1;

void backlight_init(void) {
    // Після пробудження з deep sleep пін ще утримується в 0 як RTC IO
    // (див. backlight_off_for_deep_sleep) - повернути його LEDC.
    rtc_gpio_hold_dis(BACKLIGHT_GPIO);
    rtc_gpio_deinit(BACKLIGHT_GPIO);

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_DUTY_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
    }

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = BACKLIGHT_GPIO,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = s_max_duty, // старт на повній яскравості, доки контролер не підхопить керування
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(err));
    }
}

void backlight_set_percent(uint8_t percent) {
    if (percent > 100) {
        percent = 100; // насичення - гранична умова, а не UB/переповнення
    }
    uint32_t duty = ((uint32_t)percent * s_max_duty) / 100;

    esp_err_t err = ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty: %s", esp_err_to_name(err));
        return;
    }
    err = ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty: %s", esp_err_to_name(err));
    }
}

void backlight_off_for_deep_sleep(void) {
    rtc_gpio_init(BACKLIGHT_GPIO);
    rtc_gpio_set_direction(BACKLIGHT_GPIO, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(BACKLIGHT_GPIO, 0);
    rtc_gpio_hold_en(BACKLIGHT_GPIO);
}
