#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "button.h"

static const char *TAG = "BUTTON";

/* Черга подій ISR -> задача. Кожен елемент - лише номер піна, що змінив
 * рівень; сама задача перечитує gpio_get_level(), тому черга може бути
 * дрібною (глибина 8 із запасом на кількадзвінкові фронти під час дребезгу). */
static QueueHandle_t s_gpio_evt_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    // З ISR можна лише xQueueSendFromISR - без блокуючих/логуючих викликів.
    xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
}

esp_err_t button_init(int gpio_num) {
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
        return ESP_ERR_NO_MEM;
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

    return ESP_OK;
}

bool button_wait_event(TickType_t wait_ticks) {
    uint32_t gpio_num;
    return xQueueReceive(s_gpio_evt_queue, &gpio_num, wait_ticks) == pdTRUE;
}

bool button_is_pressed(int gpio_num) {
    return gpio_get_level(gpio_num) == 0;
}
