#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "app_state.h"

static const char *TAG = "app_state";

typedef struct {
    app_state_t   state;
    wifi_status_t wifi_status;

    bool alarm_active;
    int  active_regions[APP_STATE_MAX_ACTIVE_REGIONS];
    int  active_region_count;

    uint8_t brightness_percent;

    int64_t  last_fetch_duration_us;
    uint32_t consecutive_fetch_failures;
} app_shared_state_t;

static app_shared_state_t s_state;
static SemaphoreHandle_t s_mutex;

/* Тайм-аут на take() навмисно короткий: критичні секції тут -- лише memcpy
 * кількох полів, тому кілька десятків мілісекунд очікування - це вже ознака
 * реальної проблеми (deadlock/зависла задача), а не нормальної конкуренції. */
#define MUTEX_WAIT_TICKS pdMS_TO_TICKS(200)

esp_err_t app_state_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.state = APP_STATE_BOOT;
    s_state.wifi_status = WIFI_STATUS_DISCONNECTED;
    s_state.brightness_percent = 50;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "не вдалось створити mutex спільного стану");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

const char *app_state_name(app_state_t s) {
    switch (s) {
        case APP_STATE_BOOT:            return "BOOT";
        case APP_STATE_WIFI_CONNECTING: return "WIFI_CONNECTING";
        case APP_STATE_PROVISIONING:    return "PROVISIONING";
        case APP_STATE_FETCHING:        return "FETCHING";
        case APP_STATE_ALARM:           return "ALARM";
        case APP_STATE_CLEAR:           return "CLEAR";
        case APP_STATE_ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void app_state_set(app_state_t new_state) {
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        ESP_LOGW(TAG, "app_state_set: mutex timeout");
        return;
    }
    if (s_state.state != new_state) {
        ESP_LOGI(TAG, "state: %s -> %s", app_state_name(s_state.state), app_state_name(new_state));
        s_state.state = new_state;
    }
    xSemaphoreGive(s_mutex);
}

app_state_t app_state_get(void) {
    app_state_t s = APP_STATE_BOOT;
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) == pdTRUE) {
        s = s_state.state;
        xSemaphoreGive(s_mutex);
    }
    return s;
}

void app_state_set_wifi_status(wifi_status_t s) {
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        return;
    }
    s_state.wifi_status = s;
    xSemaphoreGive(s_mutex);
}

wifi_status_t app_state_get_wifi_status(void) {
    wifi_status_t s = WIFI_STATUS_DISCONNECTED;
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) == pdTRUE) {
        s = s_state.wifi_status;
        xSemaphoreGive(s_mutex);
    }
    return s;
}

void app_state_set_alarm(bool alarm_active, const int *regions, int count) {
    if (count < 0) {
        count = 0;
    }
    if (count > APP_STATE_MAX_ACTIVE_REGIONS) {
        count = APP_STATE_MAX_ACTIVE_REGIONS; // захист від переповнення масиву (гранична умова)
    }

    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        ESP_LOGW(TAG, "app_state_set_alarm: mutex timeout");
        return;
    }
    s_state.alarm_active = alarm_active;
    s_state.active_region_count = count;
    if (count > 0 && regions != NULL) {
        memcpy(s_state.active_regions, regions, sizeof(int) * (size_t)count);
    }
    xSemaphoreGive(s_mutex);
}

void app_state_get_alarm(bool *alarm_active, int *regions_out, int *count_out) {
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        if (alarm_active) *alarm_active = false;
        if (count_out) *count_out = 0;
        return;
    }
    if (alarm_active) {
        *alarm_active = s_state.alarm_active;
    }
    if (count_out) {
        *count_out = s_state.active_region_count;
    }
    if (regions_out && s_state.active_region_count > 0) {
        memcpy(regions_out, s_state.active_regions, sizeof(int) * (size_t)s_state.active_region_count);
    }
    xSemaphoreGive(s_mutex);
}

bool app_state_is_alarm_active(void) {
    bool active = false;
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) == pdTRUE) {
        active = s_state.alarm_active;
        xSemaphoreGive(s_mutex);
    }
    return active;
}

void app_state_set_brightness(uint8_t percent) {
    if (percent > 100) {
        percent = 100; // насичення (гранична умова)
    }
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) == pdTRUE) {
        s_state.brightness_percent = percent;
        xSemaphoreGive(s_mutex);
    }
}

uint8_t app_state_get_brightness(void) {
    uint8_t p = 50;
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) == pdTRUE) {
        p = s_state.brightness_percent;
        xSemaphoreGive(s_mutex);
    }
    return p;
}

void app_state_record_fetch(int64_t duration_us, bool ok) {
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        return;
    }
    s_state.last_fetch_duration_us = duration_us;
    if (ok) {
        s_state.consecutive_fetch_failures = 0;
    } else if (s_state.consecutive_fetch_failures < UINT32_MAX) {
        s_state.consecutive_fetch_failures++;
    }
    xSemaphoreGive(s_mutex);
}

void app_state_get_fetch_stats(int64_t *last_duration_us, uint32_t *consecutive_failures) {
    if (xSemaphoreTake(s_mutex, MUTEX_WAIT_TICKS) != pdTRUE) {
        return;
    }
    if (last_duration_us) *last_duration_us = s_state.last_fetch_duration_us;
    if (consecutive_failures) *consecutive_failures = s_state.consecutive_fetch_failures;
    xSemaphoreGive(s_mutex);
}
