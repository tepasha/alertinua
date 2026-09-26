#pragma once
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#define SETUP_BUTTON_GPIO GPIO_NUM_0
#define LONG_PRESS_MS 3000           // hold for 3s to enter WiFi setup

/*
 * Драйвер кнопки: налаштовує gpio_num як вхід з перериваннями (обидва фронти)
 * і складає події з ISR у внутрішню чергу. Саму логіку дебаунсу й long-press
 * виконує button_task (task/button_task/button_task.c).
 *
 * Задача більшість часу спить на button_wait_event() і прокидається лише на
 * реальну подію - зміну рівня на піні (без busy-poll gpio_get_level()).
 */
esp_err_t button_init(int gpio_num);

/* Чекає на подію зміни рівня з ISR не довше wait_ticks. true - подія була. */
bool button_wait_event(TickType_t wait_ticks);

/* true, якщо кнопка зараз натиснута (активний низький рівень, pull-up). */
bool button_is_pressed(int gpio_num);
