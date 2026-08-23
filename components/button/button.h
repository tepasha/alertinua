#pragma once

typedef void (*button_long_press_cb_t)(void);
void button_start_long_press_watch(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press);
