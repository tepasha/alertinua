#pragma once
#include <stdint.h>

/*
 * Низькорівневий драйвер п'єзозумера на LEDC (PWM). Нічого не блокує і не
 * має власної задачі - сирену/біпи й командну чергу реалізує buzzer_task
 * (task/buzzer_task/buzzer_task.c).
 */

void buzzer_hw_init(void);                 // налаштувати LEDC-таймер і канал (один раз)
void buzzer_hw_on(void);                   // шпаруватість 50% на поточній частоті
void buzzer_hw_off(void);                  // тиша
void buzzer_hw_set_freq(uint32_t freq_hz); // змінити частоту тону
