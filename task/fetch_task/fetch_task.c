#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "app_state.h"
#include "scraping.h"
#include "alerts_parser.h"
#include "alert_msg.h"
#include "settings.h"
#include "fetch_task.h"

static const char *TAG = "FETCH";

/* URL і токен API беруться з .env (env/env.py генерує env/env_config.h).
 * Якщо генератор ще не запускався - заголовка немає, і діють значення нижче. */
#if __has_include("env_config.h")
#include "env_config.h"
#endif
#ifndef ALERTINUA_API_URL
#define ALERTINUA_API_URL "https://api.alerts.in.ua/v1/iot/active_air_raid_alerts_by_oblast.json"
#endif
#ifndef ALERTINUA_API_TOKEN
#define ALERTINUA_API_TOKEN ""
#endif
#ifndef CONFIG_ALERTINUA_POLL_INTERVAL_SEC
#define CONFIG_ALERTINUA_POLL_INTERVAL_SEC 60
#endif

/* Відповідь IoT-ендпоінта - рядок на 27 символів у лапках (~30 байт);
 * 256 - із запасом на пробіли/перенос рядка. */
#define HTTP_RESPONSE_BUF_SIZE 256

static QueueHandle_t s_alert_queue = NULL;

/* Сон, сумісний із Task Watchdog (тайм-аут 10с): шматками по 1с зі скиданням. */
static void wdt_friendly_delay_ms(uint32_t ms) {
    while (ms > 0) {
        uint32_t chunk = (ms > 1000) ? 1000 : ms;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(chunk));
        ms -= chunk;
    }
    esp_task_wdt_reset();
}

/* ------------------------------------------------------------------------ */
/* fetch_task: періодично опитує alerts.in.ua, парсить JSON, шле результат  */
/* у alert_queue. Буфер відповіді - static (виділений один раз у .bss), щоб */
/* не робити malloc/free на кожен цикл опитування (п.6.3 - без надлишкових  */
/* копій/перевиділень).                                                     */
/* ------------------------------------------------------------------------ */
static void fetch_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    static char response_body[HTTP_RESPONSE_BUF_SIZE];

    // Область задається через веб-інтерфейс і зберігається в NVS; зміна
    // застосовується після перезавантаження, яке сторінка робить сама.
    char oblast[SETTINGS_OBLAST_MAX_LEN];
    settings_get_oblast(oblast, sizeof(oblast));
    ESP_LOGI(TAG, "область для стеження: \"%s\"", oblast);
    const uint32_t poll_ms = (uint32_t)CONFIG_ALERTINUA_POLL_INTERVAL_SEC * 1000U;

    while (1) {
        esp_task_wdt_reset();

        if (app_state_get_wifi_status() != WIFI_STATUS_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        app_state_set(APP_STATE_FETCHING);

        // DNS + TLS + HTTP разом можуть тривати довше за тайм-аут watchdog
        // (10с), а скинути його посеред esp_http_client_perform() нема як.
        // Тому на час запиту задача відписується від watchdog - зависання тут
        // обмежує власний тайм-аут HTTP-клієнта (див. scraping.c).
        esp_task_wdt_delete(NULL);
        int64_t t_fetch_start = esp_timer_get_time();
        int status = api_fetch_bearer_auth(ALERTINUA_API_URL, ALERTINUA_API_TOKEN,
                                            response_body, sizeof(response_body));
        int64_t fetch_us = esp_timer_get_time() - t_fetch_start;
        esp_task_wdt_add(NULL);

        alert_msg_t msg = { 0 };

        if (status >= 200 && status < 300) {
            int64_t t_parse_start = esp_timer_get_time();
            bool ok = alerts_parse(response_body, oblast, &msg.parsed);
            int64_t parse_us = esp_timer_get_time() - t_parse_start;

            ESP_LOGI(TAG, "fetch=%lld us parse=%lld us alarm=%d full=%d partial=%d",
                     (long long)fetch_us, (long long)parse_us,
                     (int)msg.parsed.alarm_active_selected, msg.parsed.active_region_count,
                     msg.parsed.partial_region_count);

            msg.fetch_ok = ok;
            app_state_record_fetch(fetch_us + parse_us, ok);

            if (ok) {
                app_state_set_alarm(msg.parsed.alarm_active_selected,
                                     msg.parsed.active_region_indices,
                                     msg.parsed.active_region_count);
                app_state_set(msg.parsed.alarm_active_selected ? APP_STATE_ALARM : APP_STATE_CLEAR);
            } else {
                ESP_LOGE(TAG, "не вдалось розібрати/валідувати відповідь API");
                app_state_set(APP_STATE_ERROR);
            }
        } else {
            ESP_LOGE(TAG, "HTTP-запит не вдався, status=%d (fetch=%lld us)", status, (long long)fetch_us);
            msg.fetch_ok = false;
            app_state_record_fetch(fetch_us, false);
            app_state_set(APP_STATE_ERROR);
        }

        if (xQueueSend(s_alert_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "render_task не встигає забирати з alert_queue - повідомлення втрачено");
        }

        wdt_friendly_delay_ms(poll_ms);
    }
}

void fetch_task_start(QueueHandle_t alert_queue) {
    if (strlen(ALERTINUA_API_TOKEN) == 0) {
        ESP_LOGW(TAG, "ALERTINUA_API_TOKEN не заданий у .env - запити до API повертатимуть 401/403");
    }
    s_alert_queue = alert_queue;
    xTaskCreate(fetch_task, "fetch_task", 6144, NULL, 4, NULL);
}
