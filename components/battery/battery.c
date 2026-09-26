#include "driver/gpio.h"
#include "esp_log.h"

#include "adc_shared.h"
#include "battery.h"

#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6 /* GPIO34 на ADC1 */
#define BATTERY_DIVIDER     2             /* дільник 100к/100к на платі */
#define BATTERY_SAMPLES     16

static const char *TAG = "BATTERY";
static bool s_ready = false;

esp_err_t battery_init(void) {
    // Увімкнути дільник напруги батареї (без цього на GPIO34 нічого немає).
    const gpio_config_t en_cfg = {
        .pin_bit_mask = 1ULL << BATTERY_ADC_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&en_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(ADC_EN): %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(BATTERY_ADC_EN_GPIO, 1);

    err = adc1_shared_add_channel(BATTERY_ADC_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

battery_status_t battery_read(void) {
    battery_status_t st = { 0 };
    if (!s_ready) {
        return st;
    }

    // Усереднення прибирає шум АЦП ESP32 (кілька десятків мВ між вибірками).
    int sum = 0;
    int ok = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        int mv = 0;
        if (adc1_shared_read_mv(BATTERY_ADC_CHANNEL, &mv) == ESP_OK) {
            sum += mv;
            ok++;
        }
    }
    if (ok == 0) {
        ESP_LOGW(TAG, "не вдалось виміряти напругу батареї");
        return st;
    }

    st.valid = true;
    st.voltage_mv = (sum / ok) * BATTERY_DIVIDER;
    st.percent = battery_percent_from_mv(st.voltage_mv);
    st.external_power = battery_is_external_power(st.voltage_mv);
    return st;
}
