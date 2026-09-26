#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Драйвер Wi-Fi: STA-підключення та режим налаштування (SoftAP + веб-форма).
 * Власної задачі не має - цикл підключення з backoff і автоматичним
 * переходом у provisioning реалізує wifi_task (task/wifi_task/wifi_task.c).
 */

typedef enum {
    WIFI_CONNECT_PENDING = 0, // ще підключаємось
    WIFI_CONNECT_OK,          // IP отримано
    WIFI_CONNECT_FAILED,      // стек вичерпав швидкі спроби
} wifi_connect_result_t;

/*
 * Починає одну спробу (пере)підключення в режимі STA і одразу повертається.
 * Перший виклик запускає стек Wi-Fi, кожен наступний лише форсує новий
 * esp_wifi_connect(). false - стек Wi-Fi не вдалось запустити.
 */
bool wifi_manager_begin_connect(const char *ssid, const char *password);

/*
 * Чекає результат спроби, почата wifi_manager_begin_connect(), не довше
 * wait_ms. Викликати короткими кроками (напр. по 1с), скидаючи watchdog між ними.
 */
wifi_connect_result_t wifi_manager_wait_connect(uint32_t wait_ms);

/*
 * Чекає до poll_ms на повідомлення про остаточну втрату зв'язку.
 * true - з'єднання ще (ймовірно) тримається; false - стек вичерпав спроби
 * перепідключення.
 */
bool wifi_manager_still_connected(uint32_t poll_ms);

/*
 * Ручний перехід у режим налаштування Wi-Fi (SoftAP "ESP32-Setup" + сторінка
 * http://192.168.4.1/). Викликається з довгого натискання кнопки (button_task)
 * або з wifi_task після серії невдалих спроб. Після збереження нових даних
 * пристрій сам робить esp_restart().
 */
void wifi_manager_start_provisioning(void);
