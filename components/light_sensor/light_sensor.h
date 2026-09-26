#pragma once
#include <stdint.h>
#include "esp_err.h"

/* LDR (фоторезистор) + резистор-дільник 10к на GPIO33 = ADC1_CH5 на
 * оригінальному ESP32 (T-Display). GPIO33 виведений на гребінку плати і
 * належить до ADC1 - ADC2 під час роботи Wi-Fi недоступний. (GPIO34 на
 * T-Display на гребінку не виведений: він зайнятий дільником напруги батареї.)
 * Дільник: 3V3 -- LDR -- (вузол = ADC) -- 10k -- GND, тож більше світла =>
 * менший опір LDR => вища напруга на вузлі => більше значення АЦП. */
#define LIGHT_SENSOR_GPIO 33

esp_err_t light_sensor_init(void);

/* 0.0 (темно) .. 1.0 (яскраво). Від'ємне значення - помилка читання АЦП. */
float light_sensor_read_normalized(void);
