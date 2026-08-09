#include "map_render.h"
#include <stdlib.h>

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

void fb_set_px(uint16_t *fb, int x, int y, uint16_t color_be)
{
    if (x < 0 || x >= MAP_DISPLAY_W || y < 0 || y >= MAP_DISPLAY_H) {
        return;
    }
    fb[y * MAP_DISPLAY_W + x] = color_be;
}

void fb_clear(uint16_t *fb, uint16_t color_be)
{
    for (int i = 0; i < MAP_DISPLAY_W * MAP_DISPLAY_H; i++) {
        fb[i] = color_be;
    }
}

void fb_draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color_be)
{
    // 1. Повне кліпування: якщо лінія гарантовано за межами екрана — ігноруємо
    if ((x0 < 0 && x1 < 0) || (x0 >= MAP_DISPLAY_W && x1 >= MAP_DISPLAY_W) ||
        (y0 < 0 && y1 < 0) || (y0 >= MAP_DISPLAY_H && y1 >= MAP_DISPLAY_H)) {
        return;
    }

    // 2. Оптимізація для строго вертикальних ліній (дуже часті в полігонах)
    if (x0 == x1) {
        int y_start = y0 < y1 ? y0 : y1;
        int y_end   = y0 < y1 ? y1 : y0;

        if (y_start < 0) y_start = 0;
        if (y_end >= MAP_DISPLAY_H) y_end = MAP_DISPLAY_H - 1;
        if (x0 < 0 || x0 >= MAP_DISPLAY_W) return;

        uint16_t *ptr = &fb[y_start * MAP_DISPLAY_W + x0];
        for (int y = y_start; y <= y_end; y++) {
            *ptr = color_be;
            ptr += MAP_DISPLAY_W; // Перехід на наступний рядок додаванням ширини
        }
        return;
    }

    // 3. Оптимізація для строго горизонтальних ліній
    if (y0 == y1) {
        int x_start = x0 < x1 ? x0 : x1;
        int x_end   = x0 < x1 ? x1 : x0;

        if (x_start < 0) x_start = 0;
        if (x_end >= MAP_DISPLAY_W) x_end = MAP_DISPLAY_W - 1;
        if (y0 < 0 || y0 >= MAP_DISPLAY_H) return;

        uint16_t *ptr = &fb[y0 * MAP_DISPLAY_W + x_start];
        int len = x_end - x_start + 1;
        for (int i = 0; i < len; i++) {
            ptr[i] = color_be;
        }
        return;
    }

    // 4. Оптимізований алгоритм Брезенхема для діагональних ліній
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    // Вказівник на поточну позицію пікселя
    uint16_t *ptr = &fb[y0 * MAP_DISPLAY_W + x0];
    int y_stride = sy * MAP_DISPLAY_W; // Крок у байтах/елементах при зміні Y

    int curr_x = x0;
    int curr_y = y0;

    while (1) {
        // Перевірка меж тільки поточних координат
        if (curr_x >= 0 && curr_x < MAP_DISPLAY_W && curr_y >= 0 && curr_y < MAP_DISPLAY_H) {
            *ptr = color_be;
        }

        if (curr_x == x1 && curr_y == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            curr_x += sx;
            ptr += sx; // Зсув вказівника по горизонталі на ±1
        }
        if (e2 <= dx) {
            err += dx;
            curr_y += sy;
            ptr += y_stride; // Зсув вказівника по вертикалі на ±MAP_DISPLAY_W
        }
    }
}

void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
    if (n < 3) {
        return;
    }

    // 1. Знаходимо вертикальні межі полігона (ymin, ymax)
    int ymin = MAP_DISPLAY_H - 1;
    int ymax = 0;

    for (int i = 0; i < n; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }

    // Кліпування по вертикалі (не малюємо за межами екрана)
    if (ymin < 0) ymin = 0;
    if (ymax >= MAP_DISPLAY_H) ymax = MAP_DISPLAY_H - 1;

    // 2. Сканування рядок за рядком
    for (int y = ymin; y <= ymax; y++) {
        int xs[32]; // Масив для X-координат перетинів
        int count = 0;

        // Знаходимо всі перетини ребер полігона з поточним рядком Y
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int x0 = pts[i].x, y0 = pts[i].y;
            int x1 = pts[j].x, y1 = pts[j].y;

            // Пропускаємо горизонтальні ребра
            if (y0 == y1) {
                continue;
            }

            // Перевіряємо, чи перетинає рядок Y дане ребро
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                // Цілочисельна інтерполяція X без використання float!
                int x = x0 + (int32_t)(y - y0) * (x1 - x0) / (y1 - y0);

                if (count < 32) {
                    xs[count++] = x;
                }
            }
        }

        if (count < 2) continue;

        // 3. Швидке сортування вставками (Insertion Sort) X-координат за зростанням
        for (int i = 1; i < count; i++) {
            int key = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > key) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = key;
        }

        // 4. Заповнення відрізків між парами перетинів
        for (int a = 0; a + 1 < count; a += 2) {
            int x_start = xs[a];
            int x_end   = xs[a + 1];

            // Кліпування по горизонталі
            if (x_start < 0) x_start = 0;
            if (x_end >= MAP_DISPLAY_W) x_end = MAP_DISPLAY_W - 1;

            if (x_start > x_end) continue;

            // Прямий запис у фреймбуфер без виклику fb_set_px()
            uint16_t *line_ptr = &fb[y * MAP_DISPLAY_W + x_start];
            int len = x_end - x_start + 1;

            // Оптимізована заливка по 32 біти (два 16-бітних пікселі одразу)
            uint32_t pair_color = ((uint32_t)color_be << 16) | color_be;
            uint32_t *ptr32 = (uint32_t*)line_ptr;

            int pairs = len / 2;
            for (int k = 0; k < pairs; k++) {
                ptr32[k] = pair_color;
            }
            if (len & 1) {
                line_ptr[len - 1] = color_be; // залишок якщо непарна довжина
            }
        }
    }
}

void fb_draw_polygon_outline(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
   if (n < 2) return;

    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        fb_draw_line(fb, pts[i].x, pts[i].y, pts[j].x, pts[j].y, color_be);
    }
}

void render_map(uint16_t *fb, int selected)
{
    fb_clear(fb, swap16(rgb565(10, 12, 22)));

    uint16_t fill_col = swap16(rgb565(163, 160, 157));
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_fill_polygon(fb, &map_points[r->point_offset], r->point_count, fill_col);
    }

    uint16_t border_col = swap16(rgb565(90, 138, 69));
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, border_col);
    }

    if (selected >= 0 && selected < MAP_NUM_REGIONS) {
        const map_region_t *r = &map_regions[selected];
        uint16_t hl = swap16(rgb565(255, 40, 40));
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, hl);
    }
}

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
