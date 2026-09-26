#include <stdlib.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include <driver/spi_master.h>

#include "map_render.h"
#include "ukraine_map_data.h"

#define PIN_MOSI GPIO_NUM_19
#define PIN_SCLK GPIO_NUM_18
#define PIN_CS   GPIO_NUM_5
#define PIN_DC   GPIO_NUM_16
#define PIN_RST  GPIO_NUM_23
#define PIN_BL   GPIO_NUM_4

#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (20 * 1000 * 1000)

static const char *TAG = "MAP_RENDERING";

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* ST7789 очікує 16-бітний колір по SPI старшим байтом вперед, а ESP32 -
 * little-endian, тому кожен піксель перед записом у буфер міняємо байтами. */
static inline uint16_t swap16(uint16_t v) {
    return (uint16_t) ((v >> 8) | (v << 8));
}

static inline void fb_set_px(uint16_t *fb, int x, int y, uint16_t color_be) {
    if (x < 0 || x >= MAP_DISPLAY_W || y < 0 || y >= MAP_DISPLAY_H) {
        return;
    }
    fb[y * MAP_DISPLAY_W + x] = color_be;
}

static void fb_clear(uint16_t *fb, uint16_t color_be) {
    for (int i = 0; i < MAP_DISPLAY_W * MAP_DISPLAY_H; i++) {
        fb[i] = color_be;
    }
}

static void fb_draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color_be) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_set_px(fb, x0, y0, color_be);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Заливка багатокутника скануванням рядків (правило "непарний-парний"),
 * коректно обробляє й опуклі, й неопуклі контури областей. */
static void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be) {
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
                float t = (float) (y - y0) / (float) (y1 - y0);
                int x = pts[i].x + (int) (t * (pts[j].x - pts[i].x));
                if (count < 48) {
                    xs[count++] = x;
                }
            }
        }
        for (int a = 0; a < count - 1; a++) {
            for (int b = a + 1; b < count; b++) {
                if (xs[b] < xs[a]) {
                    int tmp = xs[a];
                    xs[a] = xs[b];
                    xs[b] = tmp;
                }
            }
        }
        for (int a = 0; a + 1 < count; a += 2) {
            for (int x = xs[a]; x <= xs[a + 1]; x++) {
                fb_set_px(fb, x, y, color_be);
            }
        }
    }
}

static void fb_draw_polygon_outline(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        fb_draw_line(fb, pts[i].x, pts[i].y, pts[j].x, pts[j].y, color_be);
    }
}

/* Те саме, але замість суцільної заливки лишає тільки діагональні лінії
 * (крок `period` пікселів, нахил 45°) - імітація штрихування Pip-Boy/
 * терміналів Fallout на монохромному дисплеї. */
static void fb_fill_polygon_hatched(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be, int period) {
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
                float t = (float) (y - y0) / (float) (y1 - y0);
                int x = pts[i].x + (int) (t * (pts[j].x - pts[i].x));
                if (count < 48) {
                    xs[count++] = x;
                }
            }
        }
        for (int a = 0; a < count - 1; a++) {
            for (int b = a + 1; b < count; b++) {
                if (xs[b] < xs[a]) {
                    int tmp = xs[a];
                    xs[a] = xs[b];
                    xs[b] = tmp;
                }
            }
        }
        for (int a = 0; a + 1 < count; a += 2) {
            for (int x = xs[a]; x <= xs[a + 1]; x++) {
                int diag = ((x - y) % period + period) % period; /* коректний mod для від'ємних x-y */
                if (diag == 0) {
                    fb_set_px(fb, x, y, color_be);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Публічне API (див. render.h)                                           */
/* ---------------------------------------------------------------------- */

void render_map(uint16_t *fb, int selected) {
    fb_clear(fb, swap16(rgb565(3, 10, 5)));

    uint16_t hatch_col = swap16(rgb565(25, 90, 30)); /* штрихування областей */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_fill_polygon_hatched(fb, &map_points[r->point_offset], r->point_count, hatch_col, 3);
    }

    uint16_t border_col = swap16(rgb565(100, 255, 100)); /* фосфорно-зелені межі */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, border_col);
    }

    if (selected >= 0 && selected < MAP_NUM_REGIONS) {
        const map_region_t *r = &map_regions[selected];
        uint16_t hl = swap16(rgb565(255, 255, 150)); /* янтарний Pip-Boy акцент */
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, hl);
    }
}

void render_map_multicolor(uint16_t *fb, int selected) {
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

void render_mark_region_red(uint16_t *fb, int region_index) {
    if (region_index < 0 || region_index >= MAP_NUM_REGIONS) {
        return;
    }
    const map_region_t *r = &map_regions[region_index];
    uint16_t red_fill = swap16(rgb565(200, 20, 20));
    uint16_t red_edge = swap16(rgb565(255, 80, 80));
    fb_fill_polygon(fb, &map_points[r->point_offset], r->point_count, red_fill);
    fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, red_edge);
}

void render_mark_regions_red(uint16_t *fb, const int *region_indices, int count) {
    for (int i = 0; i < count; i++) {
        render_mark_region_red(fb, region_indices[i]);
    }
}

/* Часткова тривога (лише в частині районів/громад області) - янтарна
 * заливка, щоб візуально відрізнялась від тривоги по всій області. */
void render_mark_region_partial(uint16_t *fb, int region_index) {
    if (region_index < 0 || region_index >= MAP_NUM_REGIONS) {
        return;
    }
    const map_region_t *r = &map_regions[region_index];
    uint16_t amber_fill = swap16(rgb565(180, 110, 0));
    uint16_t amber_edge = swap16(rgb565(255, 190, 40));
    fb_fill_polygon(fb, &map_points[r->point_offset], r->point_count, amber_fill);
    fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, amber_edge);
}

void render_mark_regions_partial(uint16_t *fb, const int *region_indices, int count) {
    for (int i = 0; i < count; i++) {
        render_mark_region_partial(fb, region_indices[i]);
    }
}

/* Власні блочні гліфи 8x8 лише для 'E' і 'R' (усе, що потрібно для "ERR") -
 * той самий формат, що й у поширених 8x8-шрифтах: bit0 = лівий стовпчик. */
static const uint8_t glyph_E[8] = {0xFF, 0x01, 0x01, 0x3F, 0x01, 0x01, 0x01, 0xFF};
static const uint8_t glyph_R[8] = {0x3F, 0x41, 0x41, 0x3F, 0x09, 0x11, 0x21, 0x41};

static void fb_draw_glyph(uint16_t *fb, int x0, int y0, const uint8_t *glyph, uint16_t color_be, int scale) {
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        fb_set_px(fb, x0 + col * scale + sx, y0 + row * scale + sy, color_be);
                    }
                }
            }
        }
    }
}

void render_draw_err_banner(uint16_t *fb) {
    const int scale = 3;
    const int glyph_w = 8 * scale;
    const int glyph_h = 8 * scale;
    const int gap = 4;
    const int text_w = glyph_w * 3 + gap * 2;
    const int pad = 8;
    const int box_w = text_w + pad * 2;
    const int box_h = glyph_h + pad * 2;
    const int box_x = (MAP_DISPLAY_W - box_w) / 2;
    const int box_y = (MAP_DISPLAY_H - box_h) / 2;

    uint16_t black = swap16(rgb565(0, 0, 0));
    uint16_t red_bright = swap16(rgb565(255, 40, 40));
    uint16_t red_dim = swap16(rgb565(120, 15, 15));

    /* чорна підкладка під панель */
    for (int y = box_y; y < box_y + box_h; y++) {
        for (int x = box_x; x < box_x + box_w; x++) {
            fb_set_px(fb, x, y, black);
        }
    }

    /* подвійна червона рамка - зовнішня яскрава, внутрішня тьмяніша */
    for (int x = box_x; x < box_x + box_w; x++) {
        fb_set_px(fb, x, box_y, red_bright);
        fb_set_px(fb, x, box_y + box_h - 1, red_bright);
    }
    for (int y = box_y; y < box_y + box_h; y++) {
        fb_set_px(fb, box_x, y, red_bright);
        fb_set_px(fb, box_x + box_w - 1, y, red_bright);
    }
    for (int x = box_x + 2; x < box_x + box_w - 2; x++) {
        fb_set_px(fb, x, box_y + 2, red_dim);
        fb_set_px(fb, x, box_y + box_h - 3, red_dim);
    }

    /* кутові HUD-засічки, що стирчать за межі панелі */
    int tick = 4;
    fb_draw_line(fb, box_x - tick, box_y - tick, box_x, box_y, red_bright);
    fb_draw_line(fb, box_x + box_w + tick, box_y - tick, box_x + box_w, box_y, red_bright);
    fb_draw_line(fb, box_x - tick, box_y + box_h + tick, box_x, box_y + box_h, red_bright);
    fb_draw_line(fb, box_x + box_w + tick, box_y + box_h + tick, box_x + box_w, box_y + box_h, red_bright);

    /* сам напис "ERR" по центру панелі */
    int text_x = box_x + pad;
    int text_y = box_y + pad;
    fb_draw_glyph(fb, text_x, text_y, glyph_E, red_bright, scale);
    fb_draw_glyph(fb, text_x + (glyph_w + gap), text_y, glyph_R, red_bright, scale);
    fb_draw_glyph(fb, text_x + (glyph_w + gap) * 2, text_y, glyph_R, red_bright, scale);
}

esp_lcd_panel_handle_t display_init(void) {
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };

    esp_err_t err = gpio_config(&bl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %d", err);
    }
    gpio_set_level(PIN_BL, 1); /* підсвітка увімкнена */

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1, /* дисплей нічого не надсилає назад */
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MAP_DISPLAY_W * MAP_DISPLAY_H * sizeof(uint16_t),
    };

    err = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %d", err);
    }

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
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) LCD_HOST, &io_config, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi: %d", err);
    }

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, /* якщо кольори переплутані - зміни на _RGB */
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789: %d", err);
    }

    err = esp_lcd_panel_reset(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_reset: %d", err);
    }
    err = esp_lcd_panel_init(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789: %d", err);
    }
    err = esp_lcd_panel_invert_color(panel_handle, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_invert_color: %d", err);
    }

    /* Панель фізично 135x240 (портрет); повертаємо в альбомну орієнтацію
     * 240x135, під яку згенеровано координати мапи. Зсув (gap) теж
     * міняється місцями разом з осями. Якщо картинка виявиться зсунутою,
     * підправ ці два числа чи прапорці mirror нижче. */
    err = esp_lcd_panel_swap_xy(panel_handle, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_swap_xy: %d", err);
    }
    err = esp_lcd_panel_mirror(panel_handle, false, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_mirror: %d", err);
    }
    err = esp_lcd_panel_set_gap(panel_handle, 40, 52);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_set_gap: %d", err);
    }
    err = esp_lcd_panel_disp_on_off(panel_handle, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off: %d", err);
    }

    return panel_handle;
}
