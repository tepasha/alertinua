#pragma once

/*
 * Замкнутий контур керування яскравістю підсвітки за датчиком освітлення
 * (feedback control, п.4.1/4.2 чек-листа):
 *
 *   світло (ADC, light_sensor) --> setpoint = min + lux*(max-min)
 *   error = setpoint - поточна_яскравість
 *   PI-регулятор (з anti-windup) --> нова_яскравість --> backlight (PWM)
 *
 * Працює як окрема задача FreeRTOS з періодом CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS.
 * Викликати один раз з app_main().
 */
void brightness_ctrl_start(void);
