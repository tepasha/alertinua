#include <string.h>
#include <sys/param.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID      "your-wifi-ssid"
#define WIFI_PASSWORD  "your-wifi-password"
#define WIFI_MAX_RETRY 5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_HTTP_OUTPUT_BUFFER 2048

static const char *TAG = "SCRAPING";
static esp_err_t err;

static EventGroupHandle_t s_wifi_event_group;

static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGE(TAG, "retrying WiFi connection (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
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

static bool wifi_connect_sta() {
    s_wifi_event_group = xEventGroupCreate();

    err = esp_netif_init();
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
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register: %s", esp_err_to_name(err));
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

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

    ESP_LOGI(TAG, "connecting to WiFi \"%s\"...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static int output_len = 0;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (output_len == 0 && evt->user_data) {
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            if (!esp_http_client_is_chunked_response(evt->client) && evt->user_data) {
                int copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - 1 - output_len));
                if (copy_len > 0) {
                    memcpy((char *)evt->user_data + output_len, evt->data, copy_len);
                    output_len += copy_len;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
        case HTTP_EVENT_DISCONNECTED:
            output_len = 0;
            break;
        default:
            break;
    }
    return ESP_OK;
}

int api_fetch_bearer_auth(const char *url, const char *token, char *outBuf) {
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = outBuf,
        .crt_bundle_attach = esp_crt_bundle_attach, // verify TLS against the bundled public CAs
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");
    int status = -1;

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d, content-length: %" PRId64, status, esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status;
}

[[maybe_unused]] static void get_api(char apiurl[], char token[]) {
    esp_err_t ret = nvs_flash_init(); // required by WiFi for calibration/config storage
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase: %s", esp_err_to_name(err));
        }

        ret = nvs_flash_init();
    }
    err = ret;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ret: %s", esp_err_to_name(err));
    }

    if (!wifi_connect_sta()) {
        ESP_LOGE(TAG, "Failed to connect to WiFi, giving up");
        return;
    }

    static char response_body[MAX_HTTP_OUTPUT_BUFFER];
    int status = api_fetch_bearer_auth(apiurl, token, response_body);

    if (status >= 200 && status < 300) {
        ESP_LOGI(TAG, "Response body:\n%s", response_body);
    } else {
        ESP_LOGE(TAG, "Fetch failed, status: %d", status);
    }
}
