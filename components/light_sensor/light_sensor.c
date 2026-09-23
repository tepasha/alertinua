#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "light_sensor.h"

#define LIGHT_SENSOR_ADC_UNIT    ADC_UNIT_1
#define LIGHT_SENSOR_ADC_CHANNEL ADC_CHANNEL_6 /* GPIO34 на ADC1 оригінального ESP32 */
#define LIGHT_SENSOR_ADC_MAX_RAW 4095           /* 12-бітний АЦП */

static const char *TAG = "LIGHT_SENSOR";
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_ready = false;

esp_err_t light_sensor_init(void) {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = LIGHT_SENSOR_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // повний діапазон 0..~3.3В
    };
    err = adc_oneshot_config_channel(s_adc_handle, LIGHT_SENSOR_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
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
    esp_err_t err = adc_oneshot_read(s_adc_handle, LIGHT_SENSOR_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_read: %s", esp_err_to_name(err));
        return -1.0f; // помилка периферії - хай викликач вирішує (тримати останнє значення)
    }

    // Гранична умова: явно обмежуємо діапазон, а не довіряємо, що АЦП завжди
    // повертає коректне значення в межах 0..4095.
    if (raw < 0) {
        raw = 0;
    } else if (raw > LIGHT_SENSOR_ADC_MAX_RAW) {
        raw = LIGHT_SENSOR_ADC_MAX_RAW;
    }

    return (float)raw / (float)LIGHT_SENSOR_ADC_MAX_RAW;
}
