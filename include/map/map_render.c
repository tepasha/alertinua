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

void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
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

void fb_draw_polygon_outline(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
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
