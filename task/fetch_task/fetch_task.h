#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * fetch_task: періодично опитує alerts.in.ua, парсить JSON і шле результат
 * (alert_msg_t, див. alert_msg.h) у alert_queue для render_task.
 */
void fetch_task_start(QueueHandle_t alert_queue);
