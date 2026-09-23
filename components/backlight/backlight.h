#pragma once
#include <stdint.h>

/*
 * Підсвітка дисплея на LEDC (PWM), окремий таймер/канал від зумера.
 * Раніше PIN_BL просто вмикався gpio_set_level(1) - "увімкнено назавжди",
 * без регулювання яскравості. Тепер це справжній аналоговий вихід (ШІМ),
 * яким керує brightness_ctrl за показаннями датчика освітлення.
 */

#define BACKLIGHT_GPIO 4

void backlight_init(void);
void backlight_set_percent(uint8_t percent); // 0..100, значення понад 100 насичуються
