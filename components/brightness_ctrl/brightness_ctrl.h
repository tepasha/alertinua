#pragma once

/*
 * Замкнутий контур керування яскравістю підсвітки за датчиком освітлення
 * (feedback control, п.4.1/4.2 чек-листа):
 *
 *   світло (ADC, light_sensor) --> setpoint = min + lux*(max-min)
 *   error = setpoint - поточна_яскравість
 *   PI-регулятор (з anti-windup) --> нова_яскравість --> backlight (PWM)
 *
 * Тут лише чиста математика одного кроку регулятора - без ESP-IDF/FreeRTOS,
 * тому вона компілюється й тестується на хості (test_host/). Періодичну
 * задачу, що читає датчик і виставляє підсвітку, реалізує brightness_task
 * (task/brightness_task/brightness_task.c).
 */

/*
 * Один крок PI-регулятора.
 *   lux             - 0.0 (темно) .. 1.0 (яскраво); від'ємне значення =
 *                     датчик недоступний: вихід і *integral не змінюються.
 *   current_percent - поточна яскравість підсвітки, %.
 *   integral        - стан інтегратора (зберігається між викликами).
 *   min_p, max_p    - межі виходу, %.
 *   period_s        - період виклику, секунди.
 * Повертає нову яскравість, насичену в межах [min_p, max_p].
 */
float brightness_ctrl_step(float lux, float current_percent, float *integral,
                           float min_p, float max_p, float period_s);
