#pragma once

#include <stdint.h>
#include "esp_lcd_panel_io.h"

void render_map(uint16_t *fb, int selected);
void render_map_multicolor(uint16_t *fb, int selected);
esp_lcd_panel_handle_t display_init();
void main_render();
