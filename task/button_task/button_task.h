#pragma once
#include "button.h"

typedef void (*button_long_press_cb_t)(void);

/*
 * Ініціалізує кнопку (button_init) і запускає задачу-дебаунсер, яка чекає на
 * події з ISR і рахує тривалість натискання через esp_timer. on_long_press
 * викликається один раз за утримання, коли воно перевищить long_press_ms.
 *
 * Раніше ця задача постійно опитувала gpio_get_level() кожні 50мс (busy-poll),
 * що марно вантажило CPU. Тепер задача більшість часу спить на черзі подій
 * і прокидається лише на реальну подію - зміну рівня на піні.
 */
void button_task_start(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press);
