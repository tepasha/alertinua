#pragma once
#include <stdint.h>
#include "esp_err.h"

/* LDR (фоторезистор) + резистор-дільник 10к на GPIO34 = ADC1_CH6 на
 * оригінальному ESP32 (T-Display). Пін входовий-лише (input-only), що ідеально
 * підходить для аналогового датчика - не потрібен як вихід ніде більше.
 * Дільник: 3V3 -- LDR -- (вузол = ADC) -- 10k -- GND, тож більше світла =>
 * менший опір LDR => вища напруга на вузлі => більше значення АЦП. */
#define LIGHT_SENSOR_GPIO 34

esp_err_t light_sensor_init(void);

/* 0.0 (темно) .. 1.0 (яскраво). Від'ємне значення - помилка читання АЦП. */
float light_sensor_read_normalized(void);
