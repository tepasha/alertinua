#include <stdlib.h>

#include "map_render.h"
#include "ukraine_map_data.h"

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline map_point_t transform_pt(map_point_t pt)
{
    map_point_t res;
    res.x = (MAP_DISPLAY_W - 1) - pt.x;
    res.y = (MAP_DISPLAY_H - 1) - pt.y;
    return res;
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

static void fb_fill_polygon(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be)
{
    if (n < 3) {
        return;
    }
    int ymin = 255, ymax = 0;
    for (int i = 0; i < n; i++) {
        map_point_t p = transform_pt(pts[i]);
        if (p.y < ymin) ymin = p.y;
        if (p.y > ymax) ymax = p.y;
    }
    for (int y = ymin; y <= ymax; y++) {
        int xs[48];
        int count = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            map_point_t p0 = transform_pt(pts[i]);
            map_point_t p1 = transform_pt(pts[j]);
            int y0 = p0.y, y1 = p1.y;
            if (y0 == y1) {
                continue;
            }
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                float t = (float)(y - y0) / (float)(y1 - y0);
                int x = p0.x + (int)(t * (p1.x - p0.x));
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
        map_point_t p0 = transform_pt(pts[i]);
        map_point_t p1 = transform_pt(pts[j]);
        fb_draw_line(fb, p0.x, p0.y, p1.x, p1.y, color_be);
    }
}

static void fb_fill_polygon_hatched(uint16_t *fb, const map_point_t *pts, int n, uint16_t color_be, int period)
{
    if (n < 3) {
        return;
    }
    int ymin = 255, ymax = 0;
    for (int i = 0; i < n; i++) {
        map_point_t p = transform_pt(pts[i]);
        if (p.y < ymin) ymin = p.y;
        if (p.y > ymax) ymax = p.y;
    }
    for (int y = ymin; y <= ymax; y++) {
        int xs[48];
        int count = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            map_point_t p0 = transform_pt(pts[i]);
            map_point_t p1 = transform_pt(pts[j]);
            int y0 = p0.y, y1 = p1.y;
            if (y0 == y1) {
                continue;
            }
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                float t = (float)(y - y0) / (float)(y1 - y0);
                int x = p0.x + (int)(t * (p1.x - p0.x));
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
                int diag = ((x - y) % period + period) % period; /* коректний mod для від'ємних x-y */
                if (diag == 0) {
                    fb_set_px(fb, x, y, color_be);
                }
            }
        }
    }
}

void render_map(uint16_t *fb, int selected)
{
    fb_clear(fb, swap16(rgb565(3, 10, 5)));

    uint16_t hatch_col = swap16(rgb565(25, 90, 30));      /* штрихування областей */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_fill_polygon_hatched(fb, &map_points[r->point_offset], r->point_count, hatch_col, 3);
    }

    uint16_t border_col = swap16(rgb565(100, 255, 100));  /* фосфорно-зелені межі */
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        const map_region_t *r = &map_regions[i];
        fb_draw_polygon_outline(fb, &map_points[r->point_offset], r->point_count, border_col);
    }

    if (selected >= 0 && selected < MAP_NUM_REGIONS) {
        const map_region_t *r = &map_regions[selected];
        uint16_t hl = swap16(rgb565(255, 255, 150));       /* янтарний Pip-Boy акцент */
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
