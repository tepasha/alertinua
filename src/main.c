#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "ukraine_map_data.h"
#include "map_render.h"
#include "read_env.h"

static const char *TAG = "ukraine_map";

#define PIN_MOSI GPIO_NUM_19
#define PIN_SCLK GPIO_NUM_18
#define PIN_CS   GPIO_NUM_5
#define PIN_DC   GPIO_NUM_16
#define PIN_RST  GPIO_NUM_23
#define PIN_BL   GPIO_NUM_4

#define BTN_NEXT GPIO_NUM_0   /* кнопка "BOOT" */
#define BTN_PREV GPIO_NUM_35  /* друга кнопка, вхід без внутр. підтяжки */

#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (20 * 1000 * 1000)

static esp_lcd_panel_handle_t display_init(void)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10, // <--- Достатньо для асинхронної передачі
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 40, 50));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return panel_handle;
}

static void buttons_init(void)
{
    gpio_config_t next_cfg = {
        .pin_bit_mask = 1ULL << BTN_NEXT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&next_cfg);

    gpio_config_t prev_cfg = {
        .pin_bit_mask = 1ULL << BTN_PREV,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&prev_cfg);
}

void app_main(void)
{
    buttons_init();

    char *token = read_env_var("TOKEN");  // Читаємо змінну середовища з .env файлу

    esp_lcd_panel_handle_t panel = display_init();

    size_t fb_size = MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t);

    // Виділяємо ДВА буфери в оперативній пам'яті з підтримкою DMA
    uint16_t *fb0 = heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint16_t *fb1 = heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (fb0 == NULL || fb1 == NULL) {
        ESP_LOGE(TAG, "Не вдалось виділити подвійний фреймбуфер DMA!");
        return;
    }

    uint16_t *current_fb = fb0;
    int selected = -1;

    // Перший рендер
    render_map(current_fb, selected);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, current_fb);

    bool prev_next_state = true;
    bool prev_prev_state = true;
    TickType_t last_next_press = 0;
    TickType_t last_prev_press = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool current_next = gpio_get_level(BTN_NEXT);
        bool current_prev = gpio_get_level(BTN_PREV);
        bool changed = false;

        if (prev_next_state && !current_next) {
            if ((now - last_next_press) > pdMS_TO_TICKS(80)) {
                selected = (selected + 1) % MAP_NUM_REGIONS;
                changed = true;
                last_next_press = now;
            }
        }

        if (prev_prev_state && !current_prev) {
            if ((now - last_prev_press) > pdMS_TO_TICKS(80)) {
                selected = (selected - 1 + MAP_NUM_REGIONS) % MAP_NUM_REGIONS;
                changed = true;
                last_prev_press = now;
            }
        }

        if (changed) {
            // Перемикаємо вказівник на активний буфер
            current_fb = (current_fb == fb0) ? fb1 : fb0;

            // Готуємо новий кадр у фоновому буфері
            render_map(current_fb, selected);

            // Надсилаємо новий кадр по SPI через DMA
            esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, current_fb);

            const map_region_t *r = &map_regions[selected];
            ESP_LOGI(TAG, "[%d/%d] %s", selected + 1, MAP_NUM_REGIONS, r->name);
        }

        prev_next_state = current_next;
        prev_prev_state = current_prev;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
