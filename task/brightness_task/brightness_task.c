#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "light_sensor.h"
#include "backlight.h"
#include "app_state.h"
#include "brightness_ctrl.h"
#include "brightness_task.h"

#ifndef CONFIG_ALERTINUA_BRIGHTNESS_MIN_PERCENT
#define CONFIG_ALERTINUA_BRIGHTNESS_MIN_PERCENT 15
#endif
#ifndef CONFIG_ALERTINUA_BRIGHTNESS_MAX_PERCENT
#define CONFIG_ALERTINUA_BRIGHTNESS_MAX_PERCENT 100
#endif
#ifndef CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS
#define CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS 500
#endif

static const char *TAG = "BRIGHTNESS_CTRL";

static void brightness_ctrl_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    float current_percent = (float)CONFIG_ALERTINUA_BRIGHTNESS_MAX_PERCENT;
    float integral = 0.0f;
    uint32_t consecutive_sensor_errors = 0;

    const TickType_t period_ticks = pdMS_TO_TICKS(CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS);
    const float period_s = (float)CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS / 1000.0f;

    float min_p = (float)CONFIG_ALERTINUA_BRIGHTNESS_MIN_PERCENT;
    float max_p = (float)CONFIG_ALERTINUA_BRIGHTNESS_MAX_PERCENT;
    if (max_p <= min_p) {
        // захист від некоректного налаштування через menuconfig (гранична умова)
        ESP_LOGW(TAG, "BRIGHTNESS_MAX (%.0f) <= BRIGHTNESS_MIN (%.0f), форсую MAX=MIN+1", max_p, min_p);
        max_p = min_p + 1.0f;
    }

    while (1) {
        esp_task_wdt_reset();

        float lux = light_sensor_read_normalized();
        if (lux < 0.0f) {
            consecutive_sensor_errors++;
            if (consecutive_sensor_errors == 1 || consecutive_sensor_errors % 40 == 0) {
                ESP_LOGW(TAG, "датчик освітлення не читається, тримаю яскравість %.0f%%", current_percent);
            }
        } else {
            consecutive_sensor_errors = 0;

            current_percent = brightness_ctrl_step(lux, current_percent, &integral, min_p, max_p, period_s);

            uint8_t percent_u8 = (uint8_t)(current_percent + 0.5f);
            backlight_set_percent(percent_u8);
            app_state_set_brightness(percent_u8);
        }

        vTaskDelay(period_ticks);
    }
}

void brightness_task_start(void) {
    esp_err_t err = light_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "light_sensor_init: %s - підсвітка залишиться на фіксованому рівні", esp_err_to_name(err));
    }

    backlight_init();
    backlight_set_percent((uint8_t)CONFIG_ALERTINUA_BRIGHTNESS_MAX_PERCENT);

    xTaskCreate(brightness_ctrl_task, "brightness_ctrl", 3072, NULL, 3, NULL);
}
