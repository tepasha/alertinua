#include <stdio.h>
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
#include "settings.h"
#include "alerts_parser.h" // список локацій API для вибору області

static const char *TAG = "wifi_manager";

#ifndef CONFIG_ALERTINUA_PROVISIONING_AP_SSID
#define CONFIG_ALERTINUA_PROVISIONING_AP_SSID "ESP32-Setup"
#endif
#ifndef CONFIG_ALERTINUA_PROVISIONING_AP_PASSWORD
#define CONFIG_ALERTINUA_PROVISIONING_AP_PASSWORD ""
#endif

#define WIFI_MAX_RETRY 5 /* швидких спроб esp_wifi_connect() підряд, керується event-хендлером нижче */

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_wifi_stack_started = false;

/* ------------------------------------------------------------------------ */
/* Сторінка налаштування Wi-Fi (SoftAP provisioning)                        */
/* ------------------------------------------------------------------------ */

/* Сторінка збирається з трьох шматків, бо список областей (<option>)
 * генерується на льоту - з позначкою поточної вибраної області. */
static const char *SETTINGS_PAGE_HEAD =
"<!DOCTYPE html>"
"<html lang=\"uk\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>alertinua // SETUP</title>"
"<style>"
"  body{font-family:sans-serif;background:#111;color:#4ade5a;display:flex;"
"       align-items:center;justify-content:center;min-height:100vh;margin:0;padding:24px;}"
"  .box{max-width:360px;width:100%;border:1px solid #1f6b2a;border-radius:10px;padding:20px;}"
"  h2{margin-top:0;} h3{margin:20px 0 4px;font-size:15px;}"
"  label{display:block;margin:12px 0 4px;font-size:13px;}"
"  .hint{font-size:12px;color:#2f9a3c;margin:4px 0 0;}"
"  input,select{width:100%;padding:8px;background:#000;border:1px solid #1f6b2a;color:#6cff7a;box-sizing:border-box;}"
"  button{width:100%;margin-top:16px;padding:10px;background:#1f6b2a;color:#fff;border:none;border-radius:4px;}"
"</style>"
"</head>"
"<body><div class=\"box\">"
"<h2>Налаштування</h2>"
"<form method=\"POST\" action=\"/save\">"
"  <h3>Область</h3>"
"  <label for=\"oblast\">Область для сирени та індикації</label>"
"  <select id=\"oblast\" name=\"oblast\">";

static const char *SETTINGS_PAGE_TAIL =
"  </select>"
"  <h3>Wi-Fi</h3>"
"  <label for=\"ssid\">Назва мережі (SSID)</label>"
"  <input id=\"ssid\" name=\"ssid\" maxlength=\"31\">"
"  <label for=\"password\">Пароль</label>"
"  <input id=\"password\" name=\"password\" type=\"password\" maxlength=\"63\">"
"  <p class=\"hint\">Залиште SSID порожнім, щоб не змінювати Wi-Fi.</p>"
"  <button type=\"submit\">Зберегти і перезавантажити</button>"
"</form>"
"</div></body></html>";

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

/* Запускає стек Wi-Fi у режимі STA. Викликається лише один раз за весь час
 * роботи пристрою (перевіряється прапорцем s_wifi_stack_started) - подальші
 * спроби підключення просто повторно викликають esp_wifi_connect() на вже
 * запущеному драйвері, без повторної ініціалізації. */
static esp_err_t wifi_stack_start_once(const char *ssid, const char *password) {
    if (s_wifi_stack_started) {
        return ESP_OK;
    }

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
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return err;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL, &instance_got_ip);

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
    err = esp_wifi_start(); // це саме спричинить WIFI_EVENT_STA_START -> esp_wifi_connect() у хендлері
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_stack_started = true;
    return ESP_OK;
}

/* Починає одну спробу (пере)підключення і одразу повертається. Перший виклик
 * запускає стек Wi-Fi (STA_START->connect стек виконує сам). Кожен наступний
 * лише очищає прапорці і форсує новий esp_wifi_connect(). Результат чекати
 * через wifi_manager_wait_connect() короткими кроками - щоб задача-власник
 * встигала скидати watchdog. */
bool wifi_manager_begin_connect(const char *ssid, const char *password) {
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            ESP_LOGE(TAG, "не вдалось створити event group Wi-Fi");
            return false;
        }
    }

    if (!s_wifi_stack_started) {
        esp_err_t err = wifi_stack_start_once(ssid, password);
        if (err != ESP_OK) {
            return false;
        }
    } else {
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        esp_wifi_connect();
    }
    return true;
}

wifi_connect_result_t wifi_manager_wait_connect(uint32_t wait_ms) {
    if (s_wifi_event_group == NULL) {
        return WIFI_CONNECT_FAILED;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(wait_ms));
    if (bits & WIFI_CONNECTED_BIT) {
        return WIFI_CONNECT_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        return WIFI_CONNECT_FAILED;
    }
    return WIFI_CONNECT_PENDING;
}

/* true, якщо з'єднання ще (ймовірно) тримається; false - якщо стек офіційно
 * повідомив про остаточну втрату зв'язку (вичерпано WIFI_MAX_RETRY спроб). */
bool wifi_manager_still_connected(uint32_t poll_ms) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(poll_ms));
    return (bits & WIFI_FAIL_BIT) == 0;
}

/* ------------------------------------------------------------------------ */
/* Provisioning: SoftAP + маленький HTTP-сервер зі сторінкою налаштування   */
/* ------------------------------------------------------------------------ */

static esp_err_t root_get_handler(httpd_req_t *req) {
    char current[SETTINGS_OBLAST_MAX_LEN];
    settings_get_oblast(current, sizeof(current));

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, SETTINGS_PAGE_HEAD);

    // value - індекс у списку локацій API: так у формі не треба передавати
    // й розкодовувати кириличні назви, а перевірка вводу - просто межі індексу.
    char option[160];
    for (int i = 0; i < ALERTS_API_LOCATIONS; i++) {
        const char *name = alerts_location_name(i);
        snprintf(option, sizeof(option), "<option value=\"%d\"%s>%s</option>",
                 i, (strcmp(name, current) == 0) ? " selected" : "", name);
        httpd_resp_sendstr_chunk(req, option);
    }

    httpd_resp_sendstr_chunk(req, SETTINGS_PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL); // кінець відповіді
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
                val_len = outSize - 1; // гранична умова: не переповнити буфер призначення
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

#define SAVE_BODY_MAX_LEN 1024 // SSID/пароль зі спецсимволами в URL-кодуванні займають до 3 байт на символ

static esp_err_t save_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= SAVE_BODY_MAX_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    // static, а не на стеку: стек задачі HTTP-сервера лише ~4КБ, а запити
    // сервер обробляє по одному, тож спільний буфер безпечний.
    static char body[SAVE_BODY_MAX_LEN];
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

    // --- Область: індекс у списку локацій API (див. root_get_handler) ---
    char oblast_idx_str[8] = { 0 };
    const char *oblast = NULL;
    if (form_get_field(body, "oblast", oblast_idx_str, sizeof(oblast_idx_str))) {
        char *end = NULL;
        long idx = strtol(oblast_idx_str, &end, 10);
        if (end != oblast_idx_str && *end == '\0') {
            oblast = alerts_location_name((int)idx); // NULL, якщо індекс поза межами
        }
    }
    if (oblast == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid oblast");
        return ESP_FAIL;
    }

    // --- Wi-Fi: необов'язково; порожній SSID - залишити збережені дані ---
    char ssid[WIFI_CREDS_SSID_MAX_LEN] = { 0 };
    char password[WIFI_CREDS_PASS_MAX_LEN] = { 0 };
    form_get_field(body, "ssid", ssid, sizeof(ssid));
    form_get_field(body, "password", password, sizeof(password)); // опціонально -- відкриті мережі без пароля

    esp_err_t err = settings_set_oblast(oblast);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save oblast: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save oblast");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "saved oblast \"%s\"", oblast);

    if (strlen(ssid) > 0) {
        err = wifi_creds_save(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to save WiFi credentials: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "saved new WiFi credentials for SSID \"%s\"", ssid);
    } else {
        ESP_LOGI(TAG, "SSID empty - keeping saved WiFi credentials");
    }

    ESP_LOGI(TAG, "settings saved, rebooting...");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<html><head><meta charset=\"UTF-8\"></head>"
                         "<body><h2>Збережено. Перезавантаження...</h2></body></html>",
                    HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1000)); // дати відповіді дійти до браузера перед перезавантаженням
    esp_restart();
    return ESP_OK; // unreachable
}

static const httpd_uri_t save_uri = {
    .uri = "/save",
    .method = HTTP_POST,
    .handler = save_post_handler,
};

static bool s_provisioning_active = false;

void wifi_manager_start_provisioning(void) {
    if (s_provisioning_active) {
        ESP_LOGW(TAG, "already in provisioning mode");
        return;
    }
    s_provisioning_active = true;

    ESP_LOGI(TAG, "entering WiFi provisioning mode (SoftAP \"%s\")", CONFIG_ALERTINUA_PROVISIONING_AP_SSID);

    esp_wifi_disconnect();
    esp_wifi_stop();

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = CONFIG_ALERTINUA_PROVISIONING_AP_SSID,
            .ssid_len = strlen(CONFIG_ALERTINUA_PROVISIONING_AP_SSID),
            .channel = 1,
            .password = CONFIG_ALERTINUA_PROVISIONING_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(CONFIG_ALERTINUA_PROVISIONING_AP_PASSWORD) == 0) {
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
        ESP_LOGI(TAG, "settings page ready: connect to \"%s\" and open http://192.168.4.1/",
                 CONFIG_ALERTINUA_PROVISIONING_AP_SSID);
    } else {
        ESP_LOGE(TAG, "failed to start web server");
    }
}
