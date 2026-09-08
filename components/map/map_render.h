#pragma once
#include <stdint.h>
#include "ukraine_map_data.h"

void render_map(uint16_t *fb, int selected);
void render_map_multicolor(uint16_t *fb, int selected);
void render_mark_region_red(uint16_t *fb, int region_index);
void render_mark_regions_red(uint16_t *fb, const int *region_indices, int count);
void render_draw_err_banner(uint16_t *fb);

esp_lcd_panel_handle_t display_init();
