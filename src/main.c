/*
 * Мапа областей України на LilyGO T-Display (ESP32 + ST7789 1.14", 135x240,
 * тут використовується в альбомній орієнтації 240x135).
 *
 * Кнопка на GPIO0  -> наступна область
 * Кнопка на GPIO35 -> попередня область
 * Обрана область підсвічується білим контуром; повна українська назва
 * друкується в serial-консоль (ESP_LOGI), бо вбудований шрифт 8x8 має лише
 * латиницю і на самому екрані показати кирилицю нічим.
 *
 * Дані меж областей згенеровано офлайн з відкритих геоданих (спрощено
 * алгоритмом Рамера-Дугласа-Пекера і спроєктовано під 240x135 px) — див.
 * ukraine_map_data.h.
 */

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

static const char *TAG = "ukraine_map";

/* ---- Пінаут LilyGO T-Display (класична ESP32-версія) ---- */
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

/* ---------------------------------------------------------------------- */
/* Кольори / framebuffer                                                  */
/* ---------------------------------------------------------------------- */

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* ST7789 очікує 16-бітний колір по SPI старшим байтом вперед, а ESP32 -
 * little-endian, тому кожен піксель перед записом у буфер міняємо байтами. */
static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline void fb_set_px(uint16_t *fb, int x, int y, uint16_t color_be)
{
    if (x < 0 || x >= MAP_DISPLAY_W || y < 0 || y >= MAP_DISPLAY_H) {
        return;
    }
    fb[y * MAP_DISPLAY_W + x] = color_be;
}

static void fb_clear(uint16_t *fb, uint16_t color_be)
{
    for (int i = 0; i < MAP_DISPLAY_W * MAP_DISPLAY_H; i++) {
        fb[i] = color_be;
    }
}

static void fb_draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color_be)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_set_px(fb, x0, y0, color_be);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Заливка багатокутника скануванням рядків (правило "непарний-парний"),
 * коректно обробляє й опуклі, й неопуклі контури областей. */
static void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
    if (n < 3) {
        return;
    }
    int ymin = 255, ymax = 0;
    for (int i = 0; i < n; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    for (int y = ymin; y <= ymax; y++) {
        int xs[48];
        int count = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int y0 = pts[i].y, y1 = pts[j].y;
            if (y0 == y1) {
                continue;
            }
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                float t = (float)(y - y0) / (float)(y1 - y0);
                int x = pts[i].x + (int)(t * (pts[j].x - pts[i].x));
                if (count < 48) {
                    xs[count++] = x;
                }
            }
        }
        for (int a = 0; a < count - 1; a++) {
            for (int b = a + 1; b < count; b++) {
                if (xs[b] < xs[a]) { int tmp = xs[a]; xs[a] = xs[b]; xs[b] = tmp; }
            }
        }
        for (int a = 0; a + 1 < count; a += 2) {
            for (int x = xs[a]; x <= xs[a + 1]; x++) {
                fb_set_px(fb, x, y, color_be);
            }
        }
    }
}

static void fb_draw_polygon_outline(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        fb_draw_line(fb, pts[i].x, pts[i].y, pts[j].x, pts[j].y, color_be);
    }
}

/* ---------------------------------------------------------------------- */
/* Ініціалізація дисплея (esp_lcd + ST7789 по SPI)                        */
/* ---------------------------------------------------------------------- */

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

static void buttons_init(void)
{
    gpio_config_t next_cfg = {
        .pin_bit_mask = 1ULL << BTN_NEXT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&next_cfg);

    /* GPIO35 - лише вхід, без внутрішньої підтяжки (на платі є зовнішня). */
    gpio_config_t prev_cfg = {
        .pin_bit_mask = 1ULL << BTN_PREV,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&prev_cfg);
}

// Малювання мапи
static void render_map(uint16_t *fb, int selected)
{
    fb_clear(fb, swap16(rgb565(10, 12, 22)));

    uint16_t fill_col = swap16(rgb565(200, 200, 200));   /* світло-сірі області */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_fill_polygon(fb, &map_points[r->point_offset], r->point_count, fill_col);
    }

    uint16_t border_col = swap16(rgb565(255, 210, 0));   /* жовті межі областей */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, border_col);
    }

    if (selected >= 0 && selected < MAP_NUM_REGIONS) {
        const map_region_t *r = &map_regions[selected];
        uint16_t hl = swap16(rgb565(255, 40, 40));       /* обрана область - червоний контур */
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, hl);
    }
}

// Кожна область своїм кольором
// render_map_multicolor() замість render_map() там, де малюється кадр.
void render_map_multicolor(uint16_t *fb, int selected)
{
    fb_clear(fb, swap16(rgb565(10, 12, 22)));
 
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_fill_polygon(fb, &map_points[r->point_offset], r->point_count, swap16(r->color));
    }
 
    uint16_t border_col = swap16(rgb565(255, 210, 0));
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, border_col);
    }
 
    if (selected >= 0 && selected < MAP_NUM_REGIONS) {
        const map_region_t *r = &map_regions[selected];
        uint16_t hl = swap16(rgb565(255, 255, 255));
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, hl);
    }
}

void app_main(void)
{
    buttons_init();
    esp_lcd_panel_handle_t panel = display_init();

    uint16_t *fb = heap_caps_malloc(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "Не вдалось виділити framebuffer (%d байт)",
                 (int)(MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t)));
        return;
    }

    int selected = -1;
    render_map(fb, selected);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb));

    ESP_LOGI(TAG, "Мапу областей України завантажено: %d областей, %d точок меж.",
             MAP_NUM_REGIONS, MAP_NUM_POINTS);
    ESP_LOGI(TAG, "Кнопка GPIO0 - наступна область, кнопка GPIO35 - попередня.");

    bool prev_next_level = true;
    bool prev_prev_level = true;

    while (1) {
        bool next_level = gpio_get_level(BTN_NEXT);
        bool prev_level = gpio_get_level(BTN_PREV);
        bool changed = false;

        if (prev_next_level && !next_level) {           /* натискання GPIO0 (спад рівня) */
            selected = (selected + 1) % MAP_NUM_REGIONS;
            changed = true;
        } else if (prev_prev_level && !prev_level) {     /* натискання GPIO35 */
            selected = (selected - 1 + MAP_NUM_REGIONS) % MAP_NUM_REGIONS;
            changed = true;
        }

        if (changed) {
            render_map(fb, selected);
            esp_lcd_panel_draw_bitmap(panel, 0, 0, MAP_DISPLAY_W, MAP_DISPLAY_H, fb);
            const map_region_t *r = &map_regions[selected];
            ESP_LOGI(TAG, "[%d/%d] %s  (точок контуру: %d)",
                     selected + 1, MAP_NUM_REGIONS, r->name, r->point_count);
        }

        prev_next_level = next_level;
        prev_prev_level = prev_level;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
