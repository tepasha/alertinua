#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "esp_lcd_panel_ops.h"

#include "app_state.h"
#include "button.h"
#include "buzzer.h"
#include "indicators.h"
#include "brightness_ctrl.h"
#include "wifi_manager.h"
#include "scraping.h"
#include "alerts_parser.h"
#include "map_render.h"

static const char *TAG = "MAIN";

#ifndef CONFIG_ALERTINUA_API_URL
#define CONFIG_ALERTINUA_API_URL "https://api.alerts.in.ua/v1/alerts/active.json"
#endif
#ifndef CONFIG_ALERTINUA_API_TOKEN
#define CONFIG_ALERTINUA_API_TOKEN ""
#endif
#ifndef CONFIG_ALERTINUA_OBLAST_NAME
#define CONFIG_ALERTINUA_OBLAST_NAME "Київська область"
#endif
#ifndef CONFIG_ALERTINUA_POLL_INTERVAL_SEC
#define CONFIG_ALERTINUA_POLL_INTERVAL_SEC 60
#endif

#define HTTP_RESPONSE_BUF_SIZE 8192

typedef struct {
    bool fetch_ok;
    alerts_result_t parsed;
} alert_msg_t;

static QueueHandle_t s_alert_queue = NULL;

static void on_setup_button_long_press(void) {
    wifi_manager_start_provisioning();
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
    const TickType_t poll_ticks = pdMS_TO_TICKS(CONFIG_ALERTINUA_POLL_INTERVAL_SEC * 1000);

    while (1) {
        esp_task_wdt_reset();

        if (app_state_get_wifi_status() != WIFI_STATUS_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        app_state_set(APP_STATE_FETCHING);

        int64_t t_fetch_start = esp_timer_get_time();
        int status = api_fetch_bearer_auth(CONFIG_ALERTINUA_API_URL, CONFIG_ALERTINUA_API_TOKEN,
                                            response_body, sizeof(response_body));
        int64_t fetch_us = esp_timer_get_time() - t_fetch_start;

        alert_msg_t msg = { 0 };

        if (status >= 200 && status < 300) {
            int64_t t_parse_start = esp_timer_get_time();
            bool ok = alerts_parse(response_body, CONFIG_ALERTINUA_OBLAST_NAME, &msg.parsed);
            int64_t parse_us = esp_timer_get_time() - t_parse_start;

            ESP_LOGI(TAG, "fetch=%lld us parse=%lld us alarm=%d regions=%d",
                     (long long)fetch_us, (long long)parse_us,
                     (int)msg.parsed.alarm_active_selected, msg.parsed.active_region_count);

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

        vTaskDelay(poll_ticks);
    }
}

/* ------------------------------------------------------------------------ */
/* render_task: єдиний "письменник" у framebuffer/дисплей. Блокується на    */
/* черзі - CPU не витрачається, поки немає нових даних від fetch_task.      */
/* ------------------------------------------------------------------------ */
static void render_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    esp_lcd_panel_handle_t panel = display_init();

    uint16_t *fb = heap_caps_malloc(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "не вдалось виділити framebuffer (%d байт) - render_task зупинена",
                 (int)(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t)));
        vTaskDelete(NULL);
        return;
    }

    render_map(fb, -1); // стартовий екран, поки ще не прийшла жодна відповідь від API
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap: %s", esp_err_to_name(err));
    }

    alert_msg_t msg;
    bool siren_currently_on = false; // щоб не засипати buzzer_task повторними start-командами щоцикл

    while (1) {
        esp_task_wdt_reset();

        if (xQueueReceive(s_alert_queue, &msg, pdMS_TO_TICKS(5000)) != pdTRUE) {
            continue; // тайм-аут лише щоб регулярно годувати watchdog, малювати нічого нового
        }

        int64_t t0 = esp_timer_get_time();

        bool want_siren = msg.fetch_ok && msg.parsed.alarm_active_selected;

        if (!msg.fetch_ok) {
            render_draw_err_banner(fb);
        } else if (msg.parsed.alarm_active_selected) {
            render_map_multicolor(fb, -1);
            render_mark_regions_red(fb, msg.parsed.active_region_indices, msg.parsed.active_region_count);
        } else {
            render_map(fb, -1);
        }

        // Команду шлемо лише на зміну стану (edge-triggered), а не щоцикл -
        // інакше при довгій тривозі командна черга зумера засмічується
        // однаковими "start" командами (див. docs/ARCHITECTURE.md).
        if (want_siren && !siren_currently_on) {
            buzzer_start_alarm();
        } else if (!want_siren && siren_currently_on) {
            buzzer_stop_alarm();
        }
        siren_currently_on = want_siren;

        err = esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap: %s", esp_err_to_name(err));
        }

        int64_t render_us = esp_timer_get_time() - t0;
        ESP_LOGI(TAG, "render took %lld us", (long long)render_us);
    }
}

/* ------------------------------------------------------------------------ */
/* monitor_task: раз на 30с друкує розподіл часу CPU між задачами           */
/* (потребує CONFIG_FREERTOS_USE_TRACE_FACILITY + _GENERATE_RUN_TIME_STATS, */
/* див. sdkconfig.defaults). Це і є те саме "виміряно завантаження CPU"     */
/* (п.6.1/6.4 чек-листа) - на реальному пристрої в логах буде видно, що     */
/* задачі майже весь час сплять на queue/semaphore/delay, а не крутяться в  */
/* busy-loop.                                                                */
/* ------------------------------------------------------------------------ */
static void monitor_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);
    static char stats_buf[1024];

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(30000));

        vTaskGetRunTimeStats(stats_buf);
        ESP_LOGI(TAG, "=== CPU usage за останні ~30с (Task / AbsTime / %%Time) ===\n%s", stats_buf);

        int64_t last_fetch_us = 0;
        uint32_t consecutive_failures = 0;
        app_state_get_fetch_stats(&last_fetch_us, &consecutive_failures);
        ESP_LOGI(TAG, "останній HTTP+JSON цикл: %lld us, невдач підряд: %u",
                 (long long)last_fetch_us, (unsigned)consecutive_failures);
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s - Wi-Fi креденшли/provisioning можуть не працювати",
                 esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(app_state_init());

    s_alert_queue = xQueueCreate(4, sizeof(alert_msg_t));
    if (s_alert_queue == NULL) {
        ESP_LOGE(TAG, "не вдалось створити alert_queue - перезавантаження");
        esp_restart();
    }

    if (strlen(CONFIG_ALERTINUA_API_TOKEN) == 0) {
        ESP_LOGW(TAG, "ALERTINUA_API_TOKEN не заданий (idf.py menuconfig) - запити до API повертатимуть 401/403");
    }

    indicators_init();
    brightness_ctrl_start();
    buzzer_init();
    button_start_long_press_watch(SETUP_BUTTON_GPIO, LONG_PRESS_MS, on_setup_button_long_press);

    xTaskCreate(render_task, "render_task", 4096, NULL, 5, NULL);
    xTaskCreate(fetch_task, "fetch_task", 6144, NULL, 4, NULL);
    xTaskCreate(monitor_task, "monitor_task", 3072, NULL, 1, NULL);

    wifi_manager_task_start();

    ESP_LOGI(TAG, "alertinua: усі фонові задачі запущені, app_main завершується");
}
