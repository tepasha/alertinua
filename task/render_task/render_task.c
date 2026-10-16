#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"

#include "map_render.h"
#include "alert_msg.h"
#include "buzzer_task.h"
#include "render_task.h"

static const char *TAG = "RENDER";

static QueueHandle_t s_alert_queue = NULL;

/* Малює лише render_task, але вимкнути панель перед deep sleep просить
 * autonomy_task - тому SPI-обмін із панеллю захищено mutex-ом, а після
 * вимкнення render_task більше нічого не шле (s_display_off). */
static SemaphoreHandle_t s_panel_lock = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_display_off = false;

static void panel_draw(const uint16_t *fb) {
    xSemaphoreTake(s_panel_lock, portMAX_DELAY);
    if (!s_display_off) {
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_panel_lock);
}

/* ------------------------------------------------------------------------ */
/* render_task: єдиний "письменник" у framebuffer/дисплей. Блокується на    */
/* черзі - CPU не витрачається, поки немає нових даних від fetch_task.      */
/* ------------------------------------------------------------------------ */
static void render_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    xSemaphoreTake(s_panel_lock, portMAX_DELAY);
    s_panel = display_init();
    xSemaphoreGive(s_panel_lock);

    uint16_t *fb = heap_caps_malloc(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "не вдалось виділити framebuffer (%d байт) - render_task зупинена",
                 (int)(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t)));
        esp_task_wdt_delete(NULL); // інакше watchdog чекатиме скидань від видаленої задачі
        vTaskDelete(NULL);
        return;
    }

    render_map(fb, -1); // стартовий екран, поки ще не прийшла жодна відповідь від API
    panel_draw(fb);

    alert_msg_t msg;
    bool siren_currently_on = false; // щоб не засипати buzzer_task повторними start-командами щоцикл

    while (1) {
        esp_task_wdt_reset();

        if (xQueueReceive(s_alert_queue, &msg, pdMS_TO_TICKS(5000)) != pdTRUE) {
            continue; // тайм-аут лише щоб регулярно годувати watchdog, малювати нічого нового
        }

        int64_t t0 = esp_timer_get_time();

        bool want_siren = msg.fetch_ok && msg.parsed.alarm_active_selected;

        // Кадр щоразу малюється з нуля на базовій мапі - щоб на екрані не
        // лишилось старих тривог після помилки чи відбою.
        render_map(fb, -1);
        if (!msg.fetch_ok) {
            render_draw_err_banner(fb); // дані невідомі - лише "ERR", без застарілих тривог
        } else {
            // Спершу часткові (янтарні), потім повні (червоні) - повна тривога
            // завжди видна поверх.
            render_mark_regions_partial(fb, msg.parsed.partial_region_indices, msg.parsed.partial_region_count);
            render_mark_regions_red(fb, msg.parsed.active_region_indices, msg.parsed.active_region_count);
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

        panel_draw(fb);

        int64_t render_us = esp_timer_get_time() - t0;
        ESP_LOGI(TAG, "render took %lld us", (long long)render_us);
    }
}

void render_task_start(QueueHandle_t alert_queue) {
    s_panel_lock = xSemaphoreCreateMutex();
    if (s_panel_lock == NULL) {
        ESP_LOGE(TAG, "не вдалось створити mutex панелі - render_task не запущена");
        return;
    }
    s_alert_queue = alert_queue;
    xTaskCreate(render_task, "render_task", 4096, NULL, 5, NULL);
}

void render_task_display_sleep(void) {
    if (s_panel_lock == NULL) {
        return; // render_task не запускалась - панель і так спить після попереднього сну
    }
    xSemaphoreTake(s_panel_lock, portMAX_DELAY);
    if (s_panel != NULL && !s_display_off) {
        esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, false);
        if (err == ESP_OK) {
            err = esp_lcd_panel_disp_sleep(s_panel, true);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "не вдалось приспати панель: %s", esp_err_to_name(err));
        }
    }
    s_display_off = true;
    xSemaphoreGive(s_panel_lock);
}
