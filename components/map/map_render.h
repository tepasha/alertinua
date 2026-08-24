#pragma once

#include <stdint.h>
#include "esp_lcd_panel_io.h"

void render_map(uint16_t *fb, int selected);
void render_map_multicolor(uint16_t *fb, int selected);
esp_lcd_panel_handle_t display_init();
void render_mark_region_red(uint16_t *fb, int region_index);
void render_mark_regions_red(uint16_t *fb, const int *region_indices, int count);

void main_render();
void main_render_mark();
