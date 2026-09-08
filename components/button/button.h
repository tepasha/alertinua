#pragma once
#include "driver/gpio.h"

#define SETUP_BUTTON_GPIO GPIO_NUM_0
#define LONG_PRESS_MS 3000           // hold for 3s to enter WiFi setup

typedef void (*button_long_press_cb_t)();
void button_start_long_press_watch(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press);
