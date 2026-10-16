#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "alert_msg.h"

/*
 * fetch_task: періодично опитує alerts.in.ua, парсить JSON і шле результат
 * (alert_msg_t, див. alert_msg.h) у alert_queue для render_task.
 */
void fetch_task_start(QueueHandle_t alert_queue);

/*
 * Один запит до API + розбір відповіді для області oblast, без власної
 * задачі й без watchdog (запит може тривати >10с - викликач сам вирішує,
 * чи відписатись від WDT). Час запиту записується в app_state. Результат
 * завжди заповнює *msg; повертає msg->fetch_ok.
 *
 * Використовують fetch_task (щоциклу) і autonomy_task (перевірка під час
 * сну) - не викликати з двох задач одночасно: буфер відповіді спільний.
 */
bool fetch_alerts_once(const char *oblast, alert_msg_t *msg);
