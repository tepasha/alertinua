#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"

#include "wifi_manager.h"
#include "wifi_creds.h"

static const char *TAG = "wifi_manager";

#define DEFAULT_WIFI_SSID     "your-wifi-ssid"
#define DEFAULT_WIFI_PASSWORD "your-wifi-password"
#define WIFI_MAX_RETRY        5

#define PROVISIONING_AP_SSID "ESP32-Setup"
#define PROVISIONING_AP_PASSWORD ""

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static void sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retrying WiFi connection (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_manager_connect_sta() {
    char ssid[WIFI_CREDS_SSID_MAX_LEN] = DEFAULT_WIFI_SSID;
    char password[WIFI_CREDS_PASS_MAX_LEN] = DEFAULT_WIFI_PASSWORD;

    if (wifi_creds_load(ssid, password) == ESP_OK) {
        ESP_LOGI(TAG, "using WiFi credentials saved in NVS (SSID \"%s\")", ssid);
    } else {
        ESP_LOGI(TAG, "no saved WiFi credentials, using compiled-in default (SSID \"%s\")", ssid);
    }

    s_wifi_event_group = xEventGroupCreate();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register: %s", esp_err_to_name(err));
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config: %s", esp_err_to_name(err));
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "connecting to WiFi \"%s\"...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static const char *SETTINGS_PAGE =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>WiFi Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
    "input{width:100%;padding:8px;margin:6px 0 16px;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#2563eb;color:#fff;border:none;border-radius:4px}</style>"
    "</head><body>"
    "<h2>WiFi Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>Network name (SSID)</label>"
    "<input name='ssid' maxlength='31' required>"
    "<label>Password</label>"
    "<input name='password' type='password' maxlength='63'>"
    "<button type='submit'>Save &amp; Reboot</button>"
    "</form></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETTINGS_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static void url_decode(char *s) {
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static bool form_get_field(const char *body, const char *key, char *out, size_t outSize) {
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val_start = p + key_len + 1;
            const char *val_end = strchr(val_start, '&');
            size_t val_len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
            if (val_len >= outSize) {
                val_len = outSize - 1;
            }
            memcpy(out, val_start, val_len);
            out[val_len] = '\0';
            url_decode(out);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    return false;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    char body[512];
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read form data");
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    char ssid[WIFI_CREDS_SSID_MAX_LEN] = { 0 };
    char password[WIFI_CREDS_PASS_MAX_LEN] = { 0 };

    if (!form_get_field(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_FAIL;
    }
    form_get_field(body, "password", password, sizeof(password)); // optional -- open networks have none

    esp_err_t err = wifi_creds_save(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save WiFi credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved new WiFi credentials for SSID \"%s\", rebooting...", ssid);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<html><body><h2>Saved. Rebooting...</h2></body></html>", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1000)); // give the response time to actually go out
    esp_restart();
    return ESP_OK; // unreachable
}

static const httpd_uri_t save_uri = {
    .uri = "/save",
    .method = HTTP_POST,
    .handler = save_post_handler,
};

static bool s_provisioning_active = false;

void wifi_manager_start_provisioning() {
    if (s_provisioning_active) {
        ESP_LOGW(TAG, "already in provisioning mode");
        return;
    }
    s_provisioning_active = true;

    ESP_LOGI(TAG, "entering WiFi provisioning mode (SoftAP \"%s\")", PROVISIONING_AP_SSID);

    esp_wifi_disconnect();
    esp_wifi_stop();

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROVISIONING_AP_SSID,
            .ssid_len = strlen(PROVISIONING_AP_SSID),
            .channel = 1,
            .password = PROVISIONING_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(PROVISIONING_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &save_uri);
        ESP_LOGI(TAG, "settings page ready: connect to \"%s\" and open http://192.168.4.1/", PROVISIONING_AP_SSID);
    } else {
        ESP_LOGE(TAG, "failed to start web server");
    }
}
