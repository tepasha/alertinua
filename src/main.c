//TODO:
//  1. Перенести код ініціалізації та всю бізнес-логіку до окремих файлів app.c / app.h.
//  2. Виключити використання макросу ESP_ERROR_CHECK(). Замінити його на обробку коду помилки ERR із виведенням відповідного інформаційного повідомлення.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "buzzer.h"
#include "map_render.h"
#include "button.h"
#include "scraping.h"
#include "wifi_manager.h"

static const char *TAG = "main";

static void on_setup_button_long_press(void) {
    wifi_manager_start_provisioning();
}

void app_main(void)
{
    // грати музику
    buzzer_init();
    buzzer_tone(523, 120);  /* C5 */
    buzzer_tone(0, 30);     /* пауза */
    buzzer_tone(659, 120);  /* E5 */
    buzzer_tone(0, 30);
    buzzer_tone(784, 200);  /* G5 */

    // рендер
    esp_lcd_panel_handle_t panel = display_init();
 
    uint16_t *fb = heap_caps_malloc(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "Не вдалось виділити framebuffer (%d байт)",
                 (int)(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t)));
        return;
    }
 
    render_map(fb, -1);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb));
    ESP_LOGI(TAG, "Мапу областей України намальовано: %d областей, %d точок меж.",
             MAP_NUM_REGIONS, MAP_NUM_POINTS);

    // read token from .env file
    //ESP_LOGI(TAG, "Read API: %s, Token: %s", ALERT_API, ALERT_TOKEN);

    //wifi
    esp_err_t ret = nvs_flash_init(); // required by WiFi and by wifi_creds
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    button_start_long_press_watch(SETUP_BUTTON_GPIO, LONG_PRESS_MS, on_setup_button_long_press);
 
    if (!wifi_manager_connect_sta()) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Hold the setup button for %d s to reconfigure.", LONG_PRESS_MS / 1000);
        return; // button_task keeps running in the background regardless
    }

    // scraping
    // static char response_body[2048];
    // char apiString = ALERT_API + "active.json";
    // int status = api_fetch_bearer_auth(ALERT_API, ALERT_TOKEN, response_body, sizeof(response_body));
    // if (status >= 200 && status < 300) {
    //     ESP_LOGI(TAG, "Response body:\n%s", response_body);
    // } else {
    //     ESP_LOGE(TAG, "Fetch failed, status: %d", status);
    // }
}
