#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "scraping.h"

static const char *TAG = "SCRAPING";

/* Контекст одного запиту - передається через esp_http_client user_data.
 * На відміну від попередньої реалізації (лічильник output_len був
 * function-static, тобто спільний для всіх викликів), тепер стан належить
 * стеку конкретного викликача і не може "протекти" між запитами. */
typedef struct {
    char  *buf;
    size_t buf_size;
    size_t written;
} http_recv_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->user_data == NULL) {
        return ESP_OK;
    }

    // Chunked-відповіді теж підходять: esp_http_client віддає сюди вже
    // розібрані дані без службових розмірів chunk-ів.
    http_recv_ctx_t *ctx = (http_recv_ctx_t *)evt->user_data;

    if (ctx->written == 0) {
        memset(ctx->buf, 0, ctx->buf_size);
    }

    // Гранична умова: ніколи не писати за межі buf_size (лишаємо місце під '\0').
    size_t remaining = (ctx->buf_size > ctx->written + 1) ? (ctx->buf_size - 1 - ctx->written) : 0;
    size_t copy_len = (size_t)evt->data_len;
    if (copy_len > remaining) {
        copy_len = remaining;
    }
    if (copy_len > 0) {
        memcpy(ctx->buf + ctx->written, evt->data, copy_len);
        ctx->written += copy_len;
    }
    return ESP_OK;
}

int api_fetch_bearer_auth(const char *url, const char *token, char *outBuf, size_t outBufSize) {
    if (url == NULL || outBuf == NULL || outBufSize == 0) {
        ESP_LOGE(TAG, "api_fetch_bearer_auth: некоректні аргументи");
        return -1;
    }
    outBuf[0] = '\0';

    http_recv_ctx_t ctx = { .buf = outBuf, .buf_size = outBufSize, .written = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach, // перевірка TLS за вбудованим набором публічних CA
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return -1;
    }

    char auth_header[600];
    int n = snprintf(auth_header, sizeof(auth_header), "Bearer %s", token ? token : "");
    if (n < 0 || (size_t)n >= sizeof(auth_header)) {
        ESP_LOGW(TAG, "токен задовгий для буфера заголовка, буде обрізаний");
    }
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    int status = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d, content-length: %" PRId64 ", отримано %u байт",
                 status, esp_http_client_get_content_length(client), (unsigned)ctx.written);
    } else {
        ESP_LOGE(TAG, "Request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status;
}
