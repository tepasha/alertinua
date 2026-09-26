#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "adc_shared.h"

static const char *TAG = "ADC1";

#define ADC_MAX_RAW 4095

static adc_oneshot_unit_handle_t s_unit = NULL;
static adc_cali_handle_t s_cali = NULL;
static SemaphoreHandle_t s_lock = NULL;

static esp_err_t ensure_unit(void) {
    if (s_unit != NULL) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        s_unit = NULL;
        return err;
    }

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "калібрування АЦП недоступне - напруга буде наближеною");
        s_cali = NULL;
    }
#endif

    return ESP_OK;
}

esp_err_t adc1_shared_add_channel(adc_channel_t channel) {
    esp_err_t err = ensure_unit();
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // повний діапазон 0..~3.1 В
    };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = adc_oneshot_config_channel(s_unit, channel, &chan_cfg);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel(%d): %s", (int)channel, esp_err_to_name(err));
    }
    return err;
}

esp_err_t adc1_shared_read_raw(adc_channel_t channel, int *raw) {
    if (s_unit == NULL || raw == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = adc_oneshot_read(s_unit, channel, raw);
    xSemaphoreGive(s_lock);

    // Гранична умова: не довіряємо, що АЦП завжди повертає 0..4095.
    if (err == ESP_OK) {
        if (*raw < 0) *raw = 0;
        if (*raw > ADC_MAX_RAW) *raw = ADC_MAX_RAW;
    }
    return err;
}

esp_err_t adc1_shared_read_mv(adc_channel_t channel, int *mv) {
    int raw = 0;
    esp_err_t err = adc1_shared_read_raw(channel, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (s_cali != NULL && adc_cali_raw_to_voltage(s_cali, raw, mv) == ESP_OK) {
        return ESP_OK;
    }
    *mv = raw * 3100 / ADC_MAX_RAW; // наближено, без калібрування
    return ESP_OK;
}
