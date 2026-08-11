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

#include "map_render.h"
#include "read_env.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
 
#include "wifi_manager.h"
#include "button.h"
#include "scraping.h"

static const char *TAG = "main";

#define PIN_MOSI GPIO_NUM_19
#define PIN_SCLK GPIO_NUM_18
#define PIN_CS   GPIO_NUM_5
#define PIN_DC   GPIO_NUM_16
#define PIN_RST  GPIO_NUM_23
#define PIN_BL   GPIO_NUM_4

// ---- API config ----
#define API_URL       "https://api.example.com/v1/resource"
#define BEARER_TOKEN  "your-bearer-token-here"
 
// ---- Setup button ----
#define SETUP_BUTTON_GPIO 0     // BOOT button on most ESP32 devkits
#define LONG_PRESS_MS     3000  // hold for 3s to enter WiFi setup

#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (20 * 1000 * 1000)

static void on_setup_button_long_press(void) {
    wifi_manager_start_provisioning();
}

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

void app_main(void)
{
    //wifi and api fetch
    esp_err_t ret = nvs_flash_init(); // required by WiFi and by wifi_creds
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    button_start_long_press_watch(SETUP_BUTTON_GPIO, LONG_PRESS_MS, on_setup_button_long_press);
 
    if (!wifi_manager_connect_sta()) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Hold the setup button for %d s to reconfigure.", LONG_PRESS_MS / 1000);
        return; // button_task keeps running in the background regardless
    }
 
    // static char response_body[2048];
    // int status = api_fetch_bearer_auth(API_URL, BEARER_TOKEN, response_body, sizeof(response_body));
 
    // if (status >= 200 && status < 300) {
    //     ESP_LOGI(TAG, "Response body:\n%s", response_body);
    // } else {
    //     ESP_LOGE(TAG, "Fetch failed, status: %d", status);
    // }
    //end wifi and api fetch

    // read token from .env file
    // char *token = read_env_var("TOKEN");  // Читаємо змінну середовища з .env файлу

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

    // рендер
    render_map(current_fb, selected);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, current_fb);

    while (1) {

        // bool current_next = gpio_get_level(BTN_NEXT);
        // bool current_prev = gpio_get_level(BTN_PREV);
        // bool changed = false;

        // if (prev_next_state && !current_next) {
        //     if ((now - last_next_press) > pdMS_TO_TICKS(80)) {
        //         selected = (selected + 1) % MAP_NUM_REGIONS;
        //         changed = true;
        //         last_next_press = now;
        //     }
        // }

        // if (prev_prev_state && !current_prev) {
        //     if ((now - last_prev_press) > pdMS_TO_TICKS(80)) {
        //         selected = (selected - 1 + MAP_NUM_REGIONS) % MAP_NUM_REGIONS;
        //         changed = true;
        //         last_prev_press = now;
        //     }
        // }

        // if (changed) {
        //     // Перемикаємо вказівник на активний буфер
        //     current_fb = (current_fb == fb0) ? fb1 : fb0;

        //     // Готуємо новий кадр у фоновому буфері
        //     render_map(current_fb, selected);

        //     // Надсилаємо новий кадр по SPI через DMA
        //     esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, current_fb);

        //     const map_region_t *r = &map_regions[selected];
        //     ESP_LOGI(TAG, "[%d/%d] %s", selected + 1, MAP_NUM_REGIONS, r->name);
        // }

        // prev_next_state = current_next;
        // prev_prev_state = current_prev;

        // vTaskDelay(pdMS_TO_TICKS(10));
    }
}
