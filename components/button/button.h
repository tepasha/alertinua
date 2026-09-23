#pragma once
#include "driver/gpio.h"

#define SETUP_BUTTON_GPIO GPIO_NUM_0
#define LONG_PRESS_MS 3000           // hold for 3s to enter WiFi setup

typedef void (*button_long_press_cb_t)(void);

/*
 * Налаштовує gpio_num як вхід з переривннями (обидва фронти) та запускає
 * окрему задачу-дебаунсер, яка чекає на події з ISR через queue і рахує
 * тривалість натискання через esp_timer. on_long_press викликається один раз
 * за утримання, коли воно перевищить long_press_ms.
 *
 * Раніше ця задача постійно опитувала gpio_get_level() кожні 50мс (busy-poll),
 * що марно вантажило CPU. Тепер задача більшість часу спить на xQueueReceive()
 * і прокидається лише на реальну подію - зміну рівня на піні.
 */
void button_start_long_press_watch(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press);
