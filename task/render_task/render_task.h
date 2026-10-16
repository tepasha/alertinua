#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * render_task: єдиний "письменник" у framebuffer/дисплей. Забирає
 * alert_msg_t з alert_queue, малює мапу/банер і вмикає/вимикає сирену.
 */
void render_task_start(QueueHandle_t alert_queue);

/*
 * Вимикає панель (DISPOFF + SLPIN) перед deep sleep; після цього render_task
 * більше нічого не малює. Безпечно викликати з будь-якої задачі і до старту
 * render_task (тоді нічого не робить). Знову панель вмикає display_init()
 * після пробудження.
 */
void render_task_display_sleep(void);
