#include "brightness_ctrl.h"

/* Коефіцієнти PI-регулятора. Підібрані для плавної (без помітного оку
 * мерехтіння/стрибків) реакції за ~2-4 цикли на різку зміну освітлення. */
#define CTRL_KP 0.6f
#define CTRL_KI 0.15f
#define CTRL_INTEGRAL_LIMIT 50.0f /* anti-windup: обмежує внесок інтегральної складової */

float brightness_ctrl_step(float lux, float current_percent, float *integral,
                           float min_p, float max_p, float period_s) {
    if (lux < 0.0f) {
        return current_percent; // датчик недоступний - тримаємо поточне значення
    }

    float setpoint = min_p + lux * (max_p - min_p);
    float error = setpoint - current_percent;

    *integral += error * period_s;
    if (*integral > CTRL_INTEGRAL_LIMIT) *integral = CTRL_INTEGRAL_LIMIT;
    if (*integral < -CTRL_INTEGRAL_LIMIT) *integral = -CTRL_INTEGRAL_LIMIT;

    float correction = CTRL_KP * error + CTRL_KI * *integral;
    float next = current_percent + correction;

    // Насичення виходу регулятора в допустимих межах (гранична умова).
    if (next > max_p) next = max_p;
    if (next < min_p) next = min_p;

    return next;
}
