#pragma once

/*
 * brightness_task: ініціалізує датчик освітлення й підсвітку і з періодом
 * CONFIG_ALERTINUA_BRIGHTNESS_PERIOD_MS крутить PI-регулятор
 * (brightness_ctrl_step, components/brightness_ctrl).
 * Викликати один раз з app_main().
 */
void brightness_task_start(void);
