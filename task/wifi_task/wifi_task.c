#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "wifi_manager.h"
#include "wifi_creds.h"
#include "app_state.h"
#include "wifi_task.h"

static const char *TAG = "wifi_task";

#ifndef CONFIG_ALERTINUA_DEFAULT_WIFI_SSID
#define CONFIG_ALERTINUA_DEFAULT_WIFI_SSID "alertinua"
#endif
#ifndef CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD
#define CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD "alertinua"
#endif
#ifndef CONFIG_ALERTINUA_AUTO_PROVISION_AFTER_FAILURES
#define CONFIG_ALERTINUA_AUTO_PROVISION_AFTER_FAILURES 10
#endif

/* ------------------------------------------------------------------------ */
/* Задача підключення з backoff + автоматичний перехід у provisioning       */
/* ------------------------------------------------------------------------ */

static void wifi_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    char ssid[WIFI_CREDS_SSID_MAX_LEN] = CONFIG_ALERTINUA_DEFAULT_WIFI_SSID;
    char password[WIFI_CREDS_PASS_MAX_LEN] = CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD;
    if (wifi_creds_load(ssid, password) == ESP_OK) {
        ESP_LOGI(TAG, "using WiFi credentials saved in NVS (SSID \"%s\")", ssid);
    } else {
        ESP_LOGW(TAG, "no saved WiFi credentials, using compiled-in default (SSID \"%s\")", ssid);
    }

    uint32_t backoff_ms = 2000;
    const uint32_t backoff_max_ms = 60000;
    uint32_t consecutive_failures = 0;

    while (1) {
        esp_task_wdt_reset();
        app_state_set(APP_STATE_WIFI_CONNECTING);
        app_state_set_wifi_status(WIFI_STATUS_CONNECTING);
        ESP_LOGI(TAG, "connecting to WiFi \"%s\"...", ssid);

        // До 30с на спробу, але чекаємо по 1с: тайм-аут watchdog - 10с.
        bool connected = false;
        if (wifi_manager_begin_connect(ssid, password)) {
            for (int waited_s = 0; waited_s < 30; waited_s++) {
                esp_task_wdt_reset();
                wifi_connect_result_t r = wifi_manager_wait_connect(1000);
                if (r != WIFI_CONNECT_PENDING) {
                    connected = (r == WIFI_CONNECT_OK);
                    break;
                }
            }
        }

        if (connected) {
            ESP_LOGI(TAG, "Wi-Fi connected");
            app_state_set_wifi_status(WIFI_STATUS_CONNECTED);
            backoff_ms = 2000;
            consecutive_failures = 0;

            bool still_up = true;
            while (still_up) {
                esp_task_wdt_reset();
                still_up = wifi_manager_still_connected(1000); // прокидається щосекунди - і для WDT, і для реакції на дисконект
            }
            ESP_LOGW(TAG, "Wi-Fi connection lost, reconnecting...");
            app_state_set_wifi_status(WIFI_STATUS_DISCONNECTED);
        } else {
            consecutive_failures++;
            app_state_set_wifi_status(WIFI_STATUS_DISCONNECTED);
            ESP_LOGE(TAG, "Failed to connect to WiFi (%u спроб поспіль)", (unsigned)consecutive_failures);

            if (consecutive_failures >= CONFIG_ALERTINUA_AUTO_PROVISION_AFTER_FAILURES) {
                ESP_LOGW(TAG, "too many failed attempts - entering WiFi setup mode (SoftAP) automatically");
                app_state_set(APP_STATE_PROVISIONING);
                app_state_set_wifi_status(WIFI_STATUS_PROVISIONING);
                wifi_manager_start_provisioning(); // після збереження нових даних сам зробить esp_restart()
                esp_task_wdt_delete(NULL); // інакше watchdog чекатиме скидань від видаленої задачі
                vTaskDelete(NULL);
                return;
            }

            for (uint32_t waited_ms = 0; waited_ms < backoff_ms; waited_ms += 1000) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            backoff_ms = (backoff_ms * 2 > backoff_max_ms) ? backoff_max_ms : backoff_ms * 2;
        }
    }
}

void wifi_task_start(void) {
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 4, NULL);
}
