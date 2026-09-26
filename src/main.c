#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "app_state.h"
#include "indicators.h"
#include "battery.h"
#include "wifi_manager.h"

/* Усі задачі FreeRTOS живуть у task/ - по папці-компоненту на задачу */
#include "alert_msg.h"
#include "wifi_task.h"
#include "fetch_task.h"
#include "render_task.h"
#include "button_task.h"
#include "buzzer_task.h"
#include "brightness_task.h"
#include "monitor_task.h"

static const char *TAG = "MAIN";

static void on_setup_button_long_press(void) {
    wifi_manager_start_provisioning();
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

    // Черга fetch_task -> render_task
    QueueHandle_t alert_queue = xQueueCreate(4, sizeof(alert_msg_t));
    if (alert_queue == NULL) {
        ESP_LOGE(TAG, "не вдалось створити alert_queue - перезавантаження");
        esp_restart();
    }

    indicators_init();
    if (battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "не вдалось ініціалізувати вимірювання батареї");
    }
    brightness_task_start();
    buzzer_task_start();
    button_task_start(SETUP_BUTTON_GPIO, LONG_PRESS_MS, on_setup_button_long_press);

    render_task_start(alert_queue);
    fetch_task_start(alert_queue);
    monitor_task_start();

    wifi_task_start();

    ESP_LOGI(TAG, "alertinua: усі фонові задачі запущені, app_main завершується");
}
