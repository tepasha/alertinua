//TODO:
//  1. Перенести код ініціалізації та всю бізнес-логіку до окремих файлів app.c / app.h.
//  2. Виключити використання макросу ESP_ERROR_CHECK(). Замінити його на обробку коду помилки ERR із виведенням відповідного інформаційного повідомлення.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "buzzer.h"
#include "map_render.h"
#include "button.h"
#include "scraping.h"
#include "wifi_manager.h"

static const char *TAG = "main";

#define PIN_MOSI GPIO_NUM_19
#define PIN_SCLK GPIO_NUM_18
#define PIN_CS   GPIO_NUM_5
#define PIN_DC   GPIO_NUM_16
#define PIN_RST  GPIO_NUM_23
#define PIN_BL   GPIO_NUM_4
 
#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (20 * 1000 * 1000)
 
#define SETUP_BUTTON_GPIO GPIO_NUM_0
#define LONG_PRESS_MS     3000           // hold for 3s to enter WiFi setup

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
    gpio_set_level(PIN_BL, 1); /* підсвітка увімкнена */

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1, /* дисплей нічого не надсилає назад */
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
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, /* якщо кольори переплутані - зміни на _RGB */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); /* потрібно для цієї IPS-панелі */

    /* Панель фізично 135x240 (портрет); повертаємо в альбомну орієнтацію
     * 240x135, під яку згенеровано координати мапи. Зсув (gap) теж
     * міняється місцями разом з осями. Якщо картинка виявиться зсунутою,
     * підправ ці два числа чи прапорці mirror нижче. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 40, 52));
 
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
 
    return panel_handle;
}

void app_main(void)
{
    // грати музику
    buzzer_init();
    buzzer_tone(523, 120);  /* C5 */
    buzzer_tone(0, 30);     /* пауза */
    buzzer_tone(659, 120);  /* E5 */
    buzzer_tone(0, 30);
    buzzer_tone(784, 200);  /* G5 */

    // рендер
    esp_lcd_panel_handle_t panel = display_init();
 
    uint16_t *fb = heap_caps_malloc(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "Не вдалось виділити framebuffer (%d байт)",
                 (int)(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t)));
        return;
    }
 
    render_map(fb, -1);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb));
    ESP_LOGI(TAG, "Мапу областей України намальовано: %d областей, %d точок меж.",
             MAP_NUM_REGIONS, MAP_NUM_POINTS);

    // read token from .env file
    //ESP_LOGI(TAG, "Read API: %s, Token: %s", ALERT_API, ALERT_TOKEN);

    //wifi
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

    // scraping
    // static char response_body[2048];
    // char apiString = ALERT_API + "active.json";
    // int status = api_fetch_bearer_auth(ALERT_API, ALERT_TOKEN, response_body, sizeof(response_body));
    // if (status >= 200 && status < 300) {
    //     ESP_LOGI(TAG, "Response body:\n%s", response_body);
    // } else {
    //     ESP_LOGE(TAG, "Fetch failed, status: %d", status);
    // }
}
