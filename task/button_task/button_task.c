#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "button.h"
#include "button_task.h"

static const char *TAG = "BUTTON";

typedef struct {
    int gpio_num;
    int long_press_ms;
    button_long_press_cb_t on_long_press;
} button_watch_args_t;

static void button_task(void *arg) {
    button_watch_args_t *args = (button_watch_args_t *)arg;

    esp_task_wdt_add(NULL);

    const TickType_t debounce_ticks = pdMS_TO_TICKS(20);
    const TickType_t poll_while_held_ticks = pdMS_TO_TICKS(50);

    bool held = false;
    bool long_press_fired = false;
    int64_t press_start_us = 0;

    while (1) {
        esp_task_wdt_reset();

        // Поки кнопка не натиснута, чекаємо подію ISR як завгодно довго - CPU
        // не витрачається. Поки утримується - прокидаємось раз на 50мс, щоб
        // виміряти, чи не настав час long-press.
        TickType_t wait = held ? poll_while_held_ticks : pdMS_TO_TICKS(2000);
        bool got_evt = button_wait_event(wait);

        if (got_evt) {
            // Апаратний дребезг контактів: почекати трохи і перечитати рівень,
            // а не довіряти миттєвому фронту.
            vTaskDelay(debounce_ticks);
            bool pressed_now = button_is_pressed(args->gpio_num);

            if (pressed_now && !held) {
                held = true;
                long_press_fired = false;
                press_start_us = esp_timer_get_time();
            } else if (!pressed_now && held) {
                held = false;
            }
        }

        if (held && !long_press_fired) {
            int64_t held_ms = (esp_timer_get_time() - press_start_us) / 1000;
            if (held_ms >= args->long_press_ms) {
                long_press_fired = true;
                ESP_LOGI(TAG, "long press detected on GPIO%d (%lld ms)", args->gpio_num, (long long)held_ms);
                args->on_long_press();
            }
        }
    }
}

void button_task_start(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press) {
    if (button_init(gpio_num) != ESP_OK) {
        return;
    }

    button_watch_args_t *args = malloc(sizeof(button_watch_args_t));
    if (args == NULL) {
        ESP_LOGE(TAG, "malloc button_watch_args_t failed");
        return;
    }
    args->gpio_num = gpio_num;
    args->long_press_ms = long_press_ms;
    args->on_long_press = on_long_press;

    xTaskCreate(button_task, "button_task", 3072, args, 5, NULL);
}
