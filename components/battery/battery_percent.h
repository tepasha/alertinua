#pragma once
#include <stdbool.h>

/*
 * Чиста математика батареї - без ESP-IDF, тестується на хості (test_host/).
 */

/* Вище цієї напруги на лінії батареї вважаємо, що плата живиться від USB
 * (зарядний контролер тримає лінію ~4.2+ В, або батареї немає зовсім). */
#define BATTERY_EXTERNAL_POWER_MV 4300

/*
 * Рівень заряду Li-Pol (3.7 В номінал) у відсотках за напругою без
 * навантаження, за типовою кривою розряду. Між точками кривої - лінійна
 * інтерполяція. Результат 0..100; поза межами кривої - насичення.
 */
int battery_percent_from_mv(int voltage_mv);

/* true - напруга вища за BATTERY_EXTERNAL_POWER_MV: живлення від USB. */
bool battery_is_external_power(int voltage_mv);
