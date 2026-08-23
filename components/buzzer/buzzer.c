#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "buzzer.h"

#define BUZZER_GPIO         GPIO_NUM_25

#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_DUTY_RES     LEDC_TIMER_10_BIT   /* шпаруватість 0..1023 */
#define BUZZER_DUTY_50PCT   512                 /* 50% - найгучніше й найчистіше для п'єзо */

void buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 2000,             /* стартова частота - buzzer_tone() перемикає на льоту */
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,                   /* тиша, поки не викликали buzzer_tone() */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    ESP_ERROR_CHECK(ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz));
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_50PCT));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    buzzer_off();
}

void buzzer_off(void)
{
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_play_siren(void)
{
    buzzer_init();

    const uint32_t freq_low = 400;
    const uint32_t freq_high = 1200;
    const uint32_t step_ms = 3;
    const int cycles = 3;
    const uint32_t step_hz = 20;
 
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_50PCT));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL));
 
    for (int c = 0; c < cycles; c++) {
        for (uint32_t f = freq_low; f < freq_high; f += step_hz) {
            ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
        for (uint32_t f = freq_high; f > freq_low; f -= step_hz) {
            ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
    }
 
    buzzer_off();
}
