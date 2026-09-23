#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "buzzer.h"

#define BUZZER_GPIO         GPIO_NUM_25

#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_DUTY_RES     LEDC_TIMER_10_BIT   /* шпаруватість 0..1023 */
#define BUZZER_DUTY_50PCT   512                 /* 50% - найгучніше й найчистіше для п'єзо */

#ifndef CONFIG_ALERTINUA_SIREN_MAX_SEC
#define CONFIG_ALERTINUA_SIREN_MAX_SEC 120 /* запобіжник, якщо Kconfig ще не застосований */
#endif

static const char *TAG = "BUZZER";

typedef enum {
    BUZZER_CMD_ALARM_START,
    BUZZER_CMD_ALARM_STOP,
    BUZZER_CMD_BEEP,
} buzzer_cmd_kind_t;

typedef struct {
    buzzer_cmd_kind_t kind;
    uint32_t freq_hz;
    uint32_t duration_ms;
} buzzer_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;
static volatile bool s_alarm_should_run = false;

static void ledc_setup(void) {
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
    }

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(err));
    }
}

static void buzzer_hw_off(void) {
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void buzzer_hw_tone_blocking(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_50PCT);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_hw_off();
}

/* Один прохід сирени "вгору-вниз" по частоті. Перевіряє команду STOP кожні
 * step_ms, тому реагує на зупинку майже миттєво, а не аж наприкінці 100
 * циклів, як було раніше. Додатково обмежена максимальною тривалістю з
 * Kconfig - якщо хтось забуде викликати buzzer_stop_alarm(), зумер сам
 * замовкне, а не гудітиме нескінченно. */
static void run_siren_until_stopped(void) {
    const uint32_t freq_low = 400;
    const uint32_t freq_high = 1200;
    const uint32_t step_ms = 40;
    const uint32_t step_hz = 20;
    const int64_t max_us = (int64_t)CONFIG_ALERTINUA_SIREN_MAX_SEC * 1000000LL;

    int64_t started_us = esp_timer_get_time();

    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_50PCT);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);

    while (s_alarm_should_run) {
        for (uint32_t f = freq_low; f < freq_high && s_alarm_should_run; f += step_hz) {
            ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));

            buzzer_cmd_t peek;
            if (xQueuePeek(s_cmd_queue, &peek, 0) == pdTRUE && peek.kind == BUZZER_CMD_ALARM_STOP) {
                s_alarm_should_run = false;
            }
            if (esp_timer_get_time() - started_us > max_us) {
                ESP_LOGW(TAG, "siren max duration (%d s) reached, auto-stop", CONFIG_ALERTINUA_SIREN_MAX_SEC);
                s_alarm_should_run = false;
            }
        }
        for (uint32_t f = freq_high; f > freq_low && s_alarm_should_run; f -= step_hz) {
            ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
    }

    buzzer_hw_off();
}

static void buzzer_task(void *arg) {
    (void)arg;
    ledc_setup();
    esp_task_wdt_add(NULL);

    buzzer_cmd_t cmd;
    while (1) {
        esp_task_wdt_reset();
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue; // тайм-аут лише щоб регулярно годувати watchdog
        }

        switch (cmd.kind) {
            case BUZZER_CMD_ALARM_START:
                s_alarm_should_run = true;
                run_siren_until_stopped();
                break;
            case BUZZER_CMD_ALARM_STOP:
                s_alarm_should_run = false;
                buzzer_hw_off();
                break;
            case BUZZER_CMD_BEEP:
                buzzer_hw_tone_blocking(cmd.freq_hz, cmd.duration_ms);
                break;
        }
    }
}

void buzzer_init(void) {
    s_cmd_queue = xQueueCreate(4, sizeof(buzzer_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "не вдалось створити командну черту зумера");
        return;
    }
    xTaskCreate(buzzer_task, "buzzer_task", 3072, NULL, 4, NULL);
}

void buzzer_start_alarm(void) {
    if (s_cmd_queue == NULL) {
        return;
    }
    buzzer_cmd_t cmd = { .kind = BUZZER_CMD_ALARM_START };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

void buzzer_stop_alarm(void) {
    if (s_cmd_queue == NULL) {
        return;
    }
    buzzer_cmd_t cmd = { .kind = BUZZER_CMD_ALARM_STOP };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (s_cmd_queue == NULL) {
        return;
    }
    buzzer_cmd_t cmd = { .kind = BUZZER_CMD_BEEP, .freq_hz = freq_hz, .duration_ms = duration_ms };
    xQueueSend(s_cmd_queue, &cmd, 0);
}
