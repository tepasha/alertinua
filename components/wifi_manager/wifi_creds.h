#pragma once

#include "esp_err.h"

#define WIFI_CREDS_SSID_MAX_LEN 32
#define WIFI_CREDS_PASS_MAX_LEN 64

esp_err_t wifi_creds_load(char *ssid, char *password);
esp_err_t wifi_creds_save(const char *ssid, const char *password);
