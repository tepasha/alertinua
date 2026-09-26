#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "battery_percent.h"

/*
 * Акумулятор Li-Pol 602030, 3.7 В, 300 мА·год (з платою захисту)
 * підключається у штатний роз'єм BAT на LILYGO T-Display; заряджається
 * вбудованим контролером плати від USB.
 *
 * Напругу батареї плата подає через дільник 100к/100к (1:2) на GPIO34
 * (ADC1_CH6). Дільник вмикається рівнем HIGH на GPIO14. Обидва піни -
 * внутрішні, на гребінку не виведені і ні з чим більше не з'єднуються.
 */
#define BATTERY_ADC_GPIO    34
#define BATTERY_ADC_EN_GPIO 14

typedef struct {
    bool valid;          // false - виміряти не вдалося
    int  voltage_mv;     // напруга на батареї, мВ
    int  percent;        // 0..100
    bool external_power; // живлення від USB (заряджається або батареї немає)
} battery_status_t;

/* Викликати один раз з app_main() до старту задач. */
esp_err_t battery_init(void);

/* Вимірює батарею (усереднення кількох вибірок АЦП, кілька мс). */
battery_status_t battery_read(void);
