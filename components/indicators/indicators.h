#pragma once

/*
 * Status LED (GPIO2) та Wi-Fi LED (GPIO15) - периферія, яку README проєкту
 * вже документував ("Апаратне підключення"), але яка ніколи не була
 * реалізована в коді. indicators_init() налаштовує обидва піни як цифрові
 * виходи і запускає періодичний esp_timer (апаратний таймер, п.2.5
 * чек-листа), що раз на CONFIG_ALERTINUA_INDICATOR_PERIOD_MS читає стан із
 * app_state (під mutex) і оновлює світлодіоди:
 *   - Status LED: блимає, поки тривога активна; вимкнений, коли тривоги немає.
 *   - Wi-Fi LED: горить постійно при підключенні, блимає під час спроби
 *     підключення, вимкнений у режимі provisioning/AP.
 */

#define STATUS_LED_GPIO 2
#define WIFI_LED_GPIO   15

void indicators_init(void);
