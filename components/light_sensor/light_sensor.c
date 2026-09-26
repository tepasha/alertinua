#include <stdbool.h>

#include "esp_log.h"

#include "adc_shared.h"
#include "light_sensor.h"

#define LIGHT_SENSOR_ADC_CHANNEL ADC_CHANNEL_5 /* GPIO33 на ADC1 оригінального ESP32 */
#define LIGHT_SENSOR_ADC_MAX_RAW 4095           /* 12-бітний АЦП */

static const char *TAG = "LIGHT_SENSOR";
static bool s_ready = false;

esp_err_t light_sensor_init(void) {
    esp_err_t err = adc1_shared_add_channel(LIGHT_SENSOR_ADC_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

float light_sensor_read_normalized(void) {
    if (!s_ready) {
        return -1.0f;
    }

    int raw = 0;
    esp_err_t err = adc1_shared_read_raw(LIGHT_SENSOR_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc1_shared_read_raw: %s", esp_err_to_name(err));
        return -1.0f; // помилка периферії - хай викликач вирішує (тримати останнє значення)
    }

    return (float)raw / (float)LIGHT_SENSOR_ADC_MAX_RAW;
}
