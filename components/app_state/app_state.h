#pragma once

/*
 * app_state - єдине джерело правди про поточний стан пристрою.
 *
 * Кілька задач FreeRTOS (wifi_task, fetch_task, render_task, brightness_ctrl_task,
 * indicators-таймер) одночасно читають і пишуть цю структуру, тому весь доступ
 * захищено одним mutex (SemaphoreHandle_t). Критичні секції короткі - лише
 * копіювання полів, без блокуючих викликів усередині take/give.
 *
 * Цей компонент навмисно не залежить ні від чого, окрім FreeRTOS/log, щоб його
 * могли підключати всі інші компоненти (button, wifi_manager, scraping,
 * brightness_ctrl, indicators) без циклічних залежностей.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_STATE_MAX_ACTIVE_REGIONS 27

typedef enum {
    APP_STATE_BOOT = 0,      // ще нічого не ініціалізовано
    APP_STATE_WIFI_CONNECTING,
    APP_STATE_PROVISIONING,  // SoftAP + веб-форма налаштування Wi-Fi
    APP_STATE_FETCHING,      // Wi-Fi є, чекаємо/обробляємо відповідь API
    APP_STATE_ALARM,         // тривога активна у вибраній області
    APP_STATE_CLEAR,         // тривоги немає, останній запит вдалий
    APP_STATE_ERROR,         // остання спроба (HTTP/JSON) невдала
} app_state_t;

typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_PROVISIONING,
} wifi_status_t;

/* Викликати один раз з app_main() до старту будь-яких інших задач. */
esp_err_t app_state_init(void);

/* --- Основний стан системи (для логів, індикації, майбутнього розширення) --- */
void        app_state_set(app_state_t new_state);
app_state_t app_state_get(void);
const char *app_state_name(app_state_t s); // для логів/діагностики

/* --- Wi-Fi --- */
void         app_state_set_wifi_status(wifi_status_t s);
wifi_status_t app_state_get_wifi_status(void);

/* --- Тривога: яка область(і) активні прямо зараз ---
 * regions/count можуть бути NULL/0, якщо викликача цікавить лише прапорець alarm.
 * Масив копіюється ЦІЛКОМ під мьютексом, тому викликач після повернення
 * функції може безпечно користуватись копією без додаткової синхронізації. */
void app_state_set_alarm(bool alarm_active, const int *regions, int count);
void app_state_get_alarm(bool *alarm_active, int *regions_out, int *count_out);
bool app_state_is_alarm_active(void); // легкий акцесор без копіювання масиву (уникає зайвих копій)

/* --- Яскравість підсвітки, яку зараз тримає контролер (0..100%) --- */
void    app_state_set_brightness(uint8_t percent);
uint8_t app_state_get_brightness(void);

/* --- Метрики продуктивності останнього циклу опитування API ---
 * (для вимірювання часу критичних ділянок, п.6.1 чек-листа). */
void app_state_record_fetch(int64_t duration_us, bool ok);
void app_state_get_fetch_stats(int64_t *last_duration_us, uint32_t *consecutive_failures);

#ifdef __cplusplus
}
#endif
