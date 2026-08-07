#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

// Піни для TTGO T-Display
#define LCD_HOST               SPI2_HOST
#define PIN_NUM_MISO           -1
#define PIN_NUM_MOSI           19
#define PIN_NUM_CLK            18
#define PIN_NUM_CS             5
#define PIN_NUM_DC             16
#define PIN_NUM_RST            23
#define PIN_NUM_BCKL           4

// Розміри Canvas під ландшафтну орієнтацію екрана
#define CANVAS_WIDTH           240
#define CANVAS_HEIGHT          135

// Структури для геометрії
typedef struct {
    uint16_t x;
    uint16_t y;
} Point2D;

#define MAP_POINTS_COUNT 16
const Point2D ukraine_contour[MAP_POINTS_COUNT] = {
    {10, 55}, {25, 30}, {80, 25}, {130, 20}, {175, 35}, {230, 60},
    {220, 95}, {180, 90}, {165, 105}, {175, 130}, {155, 125},
    {145, 100}, {110, 110}, {85, 125}, {55, 85}, {10, 55}
};

// Глобальний вказівник на наш Canvas (буфер екрана)
uint16_t *canvas_buffer = NULL;

// Функція встановлення кольору окремого пікселя на Canvas
void canvas_draw_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT) {
        // Байт-свап (реверс) кольору для правильного відображення через SPI
        canvas_buffer[y * CANVAS_WIDTH + x] = (color << 8) | (color >> 8);
    }
}

// Стандартний алгоритм Брезенгема для малювання ліній на Canvas
void canvas_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        canvas_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void app_main(void)
{
    // 1. Виділяємо пам'ять під Canvas (240 * 135 * 2 байти = ~64 КБ)
    // Використовуємо MALLOC_CAP_DMA для максимально швидкої передачі по SPI
    canvas_buffer = heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (canvas_buffer == NULL) {
        ESP_LOGE("MAIN", "Не вдалося виділити пам'ять під Canvas!");
        return;
    }

    // Очищаємо Canvas (заливаємо темно-синім кольором RGB565: 0x000F)
    memset(canvas_buffer, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t));

    // 2. Ініціалізація підсвітки
    gpio_config_t bk_gpio_config = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_NUM_BCKL };
    gpio_config(&bk_gpio_config);
    gpio_set_level(PIN_NUM_BCKL, 1);

    // 3. Ініціалізація шини SPI
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_CLK, .mosi_io_num = PIN_NUM_MOSI, .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 4. Панель керування (Panel IO)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC, .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 26 * 1000 * 1000, // 26 MHz
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // 5. Драйвер ST7789
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST, .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    esp_lcd_panel_invert_color(panel_handle, true);

    // Поворот екрана в ландшафтний режим (240x135)
    esp_lcd_panel_mirror(panel_handle, false, false);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_set_gap(panel_handle, 40, 52); // Зміщення для T-Display

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // ----------------------------------------------------------------
    // 6. МАЛЮВАННЯ НА CANVAS
    // ----------------------------------------------------------------

    // Малюємо контур України (з'єднуємо точки лініями жовтого кольору 0xFFE0)
    uint16_t yellow_color = 0xFFE0;
    for (int i = 0; i < MAP_POINTS_COUNT - 1; i++) {
        canvas_draw_line(
            ukraine_contour[i].x,   ukraine_contour[i].y,
            ukraine_contour[i+1].x, ukraine_contour[i+1].y,
            yellow_color
        );
    }

    // Додатково: Поставимо дві жирні точки на Canvas (наприклад, Київ та Харків)
    // Київ (приблизно центр півночі)
    canvas_draw_pixel(115, 45, 0xF800); // Червона точка
    canvas_draw_pixel(116, 45, 0xF800);
    canvas_draw_pixel(115, 46, 0xF800);

    // Харків (ближче до сходу)
    canvas_draw_pixel(185, 50, 0xF800);
    canvas_draw_pixel(186, 50, 0xF800);
    canvas_draw_pixel(185, 51, 0xF800);

    // 7. Відправляємо готовий Canvas-буфер на екран одним махом
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, canvas_buffer);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}