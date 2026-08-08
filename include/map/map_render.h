#ifndef MAP_RENDER_H
#define MAP_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include "ukraine_map_data.h"

// Допоміжні функції для кольорів та фреймбуфера
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
uint16_t swap16(uint16_t v);

void fb_set_px(uint16_t *fb, int x, int y, uint16_t color_be);
void fb_clear(uint16_t *fb, uint16_t color_be);
void fb_draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color_be);
void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be);
void fb_draw_polygon_outline(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be);

// Основні функції малювання мапи
void render_map(uint16_t *fb, int selected);
void render_map_multicolor(uint16_t *fb, int selected);

#endif // MAP_RENDER_H
