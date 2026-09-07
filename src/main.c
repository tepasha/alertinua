#include <esp_log.h>
#include <nvs_flash.h>

#include "components/wifi_manager/wifi_manager.h"
#include "components/map/map_render.h"
#include "components/button/button.h"
#include "components/scraping/scraping.h"

static const char *TAG = "MAIN";

static void on_setup_button_long_press() {
    wifi_manager_start_provisioning();
}

void app_main() {
    // read token from .env file
    ESP_LOGE(TAG, "Read API: %s, Token: %s", ALERT_API, ALERT_TOKEN);

    if (!ALERT_API || !ALERT_TOKEN) {
        ESP_LOGE(TAG,"Can't read API and Token");
        return;
    }

    //wifi
    esp_err_t ret = nvs_flash_init(); // required by WiFi and by wifi_creds
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!wifi_manager_connect_sta()) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Hold the setup button for %d s to reconfigure.", LONG_PRESS_MS / 1000);
        button_start_long_press_watch(SETUP_BUTTON_GPIO, LONG_PRESS_MS, on_setup_button_long_press);
    } else {
        //scraping
        static char response_body[2048];
        int status = api_fetch_bearer_auth(ALERT_API, ALERT_TOKEN, response_body);
        if (status >= 200 && status < 300) {
            ESP_LOGI(TAG, "Response body:\n%s", response_body);
        } else {
            ESP_LOGE(TAG, "Fetch failed, status: %d", status);
        }
    }
}
