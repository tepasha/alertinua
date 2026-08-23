#pragma once

#include <stdint.h>
#include "ukraine_map_data.h"

void render_map(uint16_t *fb, int selected);
void render_map_multicolor(uint16_t *fb, int selected);
