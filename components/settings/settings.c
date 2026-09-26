#include <string.h>

#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "settings.h"

#ifndef CONFIG_ALERTINUA_OBLAST_NAME
#define CONFIG_ALERTINUA_OBLAST_NAME "Київська область"
#endif

#define NVS_NAMESPACE  "settings"
#define NVS_KEY_OBLAST "oblast"

static const char *TAG = "settings";

void settings_get_oblast(char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = out_size;
        esp_err_t err = nvs_get_str(handle, NVS_KEY_OBLAST, out, &len);
        nvs_close(handle);
        if (err == ESP_OK && out[0] != '\0') {
            return;
        }
    }

    // У NVS нічого немає (або помилка читання) - значення за замовчуванням
    strncpy(out, CONFIG_ALERTINUA_OBLAST_NAME, out_size - 1);
    out[out_size - 1] = '\0';
}

esp_err_t settings_set_oblast(const char *name) {
    if (name == NULL || strlen(name) >= SETTINGS_OBLAST_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_OBLAST, name);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
