#include <stdbool.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "button.h"

static const char *TAG = "button";

typedef struct {
    int gpio_num;
    int long_press_ms;
    button_long_press_cb_t on_long_press;
} button_watch_args_t;

static void button_task(void *arg) {
    button_watch_args_t *args = (button_watch_args_t *)arg;
    const int poll_ms = 50;
    int held_ms = 0;
    bool fired = false;

    while (1) {
        bool pressed = (gpio_get_level(args->gpio_num) == 0);

        if (pressed) {
            held_ms += poll_ms;
            if (held_ms >= args->long_press_ms && !fired) {
                fired = true;
                ESP_LOGI(TAG, "long press detected on GPIO%d", args->gpio_num);
                args->on_long_press();
            }
        } else {
            held_ms = 0;
            fired = false;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

void button_start_long_press_watch(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    button_watch_args_t *args = malloc(sizeof(button_watch_args_t));
    args->gpio_num = gpio_num;
    args->long_press_ms = long_press_ms;
    args->on_long_press = on_long_press;

    xTaskCreate(button_task, "button_task", 2048, args, 5, NULL);
}
