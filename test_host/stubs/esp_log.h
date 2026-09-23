#pragma once
/*
 * Мінімальна заглушка esp_log.h ЛИШЕ для хост-тестів (test_host/), щоб
 * компонентні .c-файли (напр. alerts_parser.c), написані проти ESP-IDF,
 * можна було скомпілювати звичайним gcc без встановленого ESP-IDF.
 * У прошивці (idf.py build) ця заглушка НЕ використовується - там підключається
 * справжній esp_log.h з ESP-IDF (компонент "log").
 */
#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
