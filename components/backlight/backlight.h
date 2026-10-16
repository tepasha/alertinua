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

/*
 * Перед deep sleep: тримає пін підсвітки в 0 (RTC hold), інакше у сні він
 * "висить" і підсвітка може тьмяно світити. Пін від'єднується від LEDC, тож
 * brightness_task уже не ввімкне його. Тримання знімає backlight_init().
 */
void backlight_off_for_deep_sleep(void);
