#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "buzzer.h"
#include "buzzer_task.h"

#ifndef CONFIG_ALERTINUA_SIREN_MAX_SEC
#define CONFIG_ALERTINUA_SIREN_MAX_SEC 5 /* якщо Kconfig ще не застосований */
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

/* Сон, сумісний із Task Watchdog (тайм-аут 10с): шматками по 1с зі скиданням. */
static void wdt_friendly_delay_ms(uint32_t ms) {
    while (ms > 0) {
        uint32_t chunk = (ms > 1000) ? 1000 : ms;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(chunk));
        ms -= chunk;
    }
}

static void buzzer_tone_blocking(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0) {
        wdt_friendly_delay_ms(duration_ms);
        return;
    }
    buzzer_hw_set_freq(freq_hz);
    buzzer_hw_on();
    wdt_friendly_delay_ms(duration_ms);
    buzzer_hw_off();
}

/* Після кожного кроку частоти: чи не прийшов STOP (відбій) і чи не вийшов
 * час сирени. Перевірка на кожному кроці (step_ms), а не раз на прохід -
 * інакше сирена звучала б до ~1.6с довше за задану тривалість. */
static void siren_check_stop(int64_t started_us, int64_t max_us) {
    buzzer_cmd_t peek;
    if (xQueuePeek(s_cmd_queue, &peek, 0) == pdTRUE && peek.kind == BUZZER_CMD_ALARM_STOP) {
        s_alarm_should_run = false;
    }
    if (s_alarm_should_run && esp_timer_get_time() - started_us >= max_us) {
        ESP_LOGI(TAG, "siren played %d s, stop", CONFIG_ALERTINUA_SIREN_MAX_SEC);
        s_alarm_should_run = false;
    }
}

/* Сирена "вгору-вниз" по частоті на початку тривоги. Звучить
 * CONFIG_ALERTINUA_SIREN_MAX_SEC секунд і замовкає сама; STOP (відбій)
 * зупиняє її раніше. */
static void run_siren_until_stopped(void) {
    const uint32_t freq_low = 400;
    const uint32_t freq_high = 1200;
    const uint32_t step_ms = 40;
    const uint32_t step_hz = 20;
    const int64_t max_us = (int64_t)CONFIG_ALERTINUA_SIREN_MAX_SEC * 1000000LL;

    int64_t started_us = esp_timer_get_time();

    buzzer_hw_on();

    while (s_alarm_should_run) {
        // Тривалість задається в Kconfig і може бути довшою за тайм-аут
        // watchdog (10с), тож скидаємо його щопроходу.
        esp_task_wdt_reset();
        for (uint32_t f = freq_low; f < freq_high && s_alarm_should_run; f += step_hz) {
            buzzer_hw_set_freq(f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
            siren_check_stop(started_us, max_us);
        }
        for (uint32_t f = freq_high; f > freq_low && s_alarm_should_run; f -= step_hz) {
            buzzer_hw_set_freq(f);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
            siren_check_stop(started_us, max_us);
        }
    }

    buzzer_hw_off();
}

static void buzzer_task(void *arg) {
    (void)arg;
    buzzer_hw_init();
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
                buzzer_tone_blocking(cmd.freq_hz, cmd.duration_ms);
                break;
        }
    }
}

void buzzer_task_start(void) {
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
