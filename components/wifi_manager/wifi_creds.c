#include "nvs.h"
#include "esp_log.h"

#include "wifi_creds.h"

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static const char *TAG = "wifi_creds";

esp_err_t wifi_creds_load(char *ssid, char *password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_len = WIFI_CREDS_SSID_MAX_LEN;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    size_t pass_len = WIFI_CREDS_PASS_MAX_LEN;
    err = nvs_get_str(handle, NVS_KEY_PASS, password, &pass_len);

    nvs_close(handle);
    return err;
}

esp_err_t wifi_creds_save(const char *ssid, const char *password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
