#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "button.h"

static const char *TAG = "BUTTON";

typedef struct {
    int gpio_num;
    int long_press_ms;
    button_long_press_cb_t on_long_press;
} button_watch_args_t;

/* Черга подій ISR -> задача. Кожен елемент - лише номер піна, що змінив
 * рівень; сама задача перечитує gpio_get_level(), тому черга може бути
 * дрібною (глибина 8 із запасом на кількадзвінкові фронти під час дребезгу). */
static QueueHandle_t s_gpio_evt_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    // З ISR можна лише xQueueSendFromISR - без блокуючих/логуючих викликів.
    xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
}

static void button_task(void *arg) {
    button_watch_args_t *args = (button_watch_args_t *)arg;

    esp_task_wdt_add(NULL);

    const TickType_t debounce_ticks = pdMS_TO_TICKS(20);
    const TickType_t poll_while_held_ticks = pdMS_TO_TICKS(50);

    bool held = false;
    bool long_press_fired = false;
    int64_t press_start_us = 0;
    uint32_t gpio_num;

    while (1) {
        esp_task_wdt_reset();

        // Поки кнопка не натиснута, чекаємо подію ISR як завгодно довго - CPU
        // не витрачається. Поки утримується - прокидаємось раз на 50мс, щоб
        // виміряти, чи не настав час long-press.
        TickType_t wait = held ? poll_while_held_ticks : pdMS_TO_TICKS(2000);
        BaseType_t got_evt = xQueueReceive(s_gpio_evt_queue, &gpio_num, wait);

        if (got_evt) {
            // Апаратний дребезг контактів: почекати трохи і перечитати рівень,
            // а не довіряти миттєвому фронту.
            vTaskDelay(debounce_ticks);
            bool pressed_now = (gpio_get_level(args->gpio_num) == 0);

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

void button_start_long_press_watch(int gpio_num, int long_press_ms, button_long_press_cb_t on_long_press) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
    }

    s_gpio_evt_queue = xQueueCreate(8, sizeof(uint32_t));
    if (s_gpio_evt_queue == NULL) {
        ESP_LOGE(TAG, "не вдалось створити queue подій кнопки");
        return;
    }

    // install_isr_service може бути вже викликаний іншим драйвером (напр.
    // іншою кнопкою) - ESP_ERR_INVALID_STATE у такому разі не є помилкою.
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
    }

    err = gpio_isr_handler_add(gpio_num, button_isr_handler, (void *)(uintptr_t)gpio_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add: %s", esp_err_to_name(err));
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
