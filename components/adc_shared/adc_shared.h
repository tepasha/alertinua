#pragma once
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

/*
 * Спільний доступ до ADC1. Драйвер oneshot дозволяє створити блок ADC1 лише
 * один раз, а читають його кілька модулів (датчик освітлення, батарея) з
 * різних задач - тож блок створюється тут один раз, а читання захищене
 * м'ютексом (функції драйвера з одним handle не потокобезпечні).
 *
 * Усі канали налаштовуються з послабленням 12 дБ (діапазон ~0..3.1 В).
 * adc1_shared_add_channel() викликати з app_main() до старту задач.
 */

esp_err_t adc1_shared_add_channel(adc_channel_t channel);

/* Сире значення 0..4095. */
esp_err_t adc1_shared_read_raw(adc_channel_t channel, int *raw);

/* Напруга на піні, мВ. З калібруванням eFuse, якщо воно доступне. */
esp_err_t adc1_shared_read_mv(adc_channel_t channel, int *mv);
