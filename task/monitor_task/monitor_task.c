#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "app_state.h"
#include "monitor_task.h"

static const char *TAG = "MONITOR";

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
        // 30с чекаємо шматками по 1с: тайм-аут Task Watchdog - 10с, і одна
        // довга vTaskDelay(30000) без esp_task_wdt_reset() перезавантажила б плату.
        for (int i = 0; i < 30; i++) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        vTaskGetRunTimeStats(stats_buf);
        ESP_LOGI(TAG, "=== CPU usage за останні ~30с (Task / AbsTime / %%Time) ===\n%s", stats_buf);

        int64_t last_fetch_us = 0;
        uint32_t consecutive_failures = 0;
        app_state_get_fetch_stats(&last_fetch_us, &consecutive_failures);
        ESP_LOGI(TAG, "останній HTTP+JSON цикл: %lld us, невдач підряд: %u",
                 (long long)last_fetch_us, (unsigned)consecutive_failures);
    }
}

void monitor_task_start(void) {
    xTaskCreate(monitor_task, "monitor_task", 3072, NULL, 1, NULL);
}
