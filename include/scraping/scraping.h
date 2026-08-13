#pragma once

#include "esp_event.h"
#include "esp_http_client.h"

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static bool wifi_connect_sta(void);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static int api_fetch_bearer_auth(const char *url, const char *token, char *outBuf, size_t outBufSize);
void get_api(void);
