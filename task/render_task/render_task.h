#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * render_task: єдиний "письменник" у framebuffer/дисплей. Забирає
 * alert_msg_t з alert_queue, малює мапу/банер і вмикає/вимикає сирену.
 */
void render_task_start(QueueHandle_t alert_queue);
